#ifndef MSGR_DISPATCHQUEUE_4616f263ca8542bdb3e45f895fc49a39_H
#define MSGR_DISPATCHQUEUE_4616f263ca8542bdb3e45f895fc49a39_H

#include <map>

#include "msgr/msgr_assert.h"
#include "msgr/Mutex.h"
#include "msgr/Cond.h"
#include "msgr/Thread.h"
#include "msgr/Connection.h"

namespace msgr {

class MsgrContext;
class Messenger;
class Message;
struct Connection;

class DispatchQueue {
public:
  class QueueItem {
    int type;
    ConnectionRef con;
    MessageRef m;
  public:
    QueueItem(Message *m) : type(-1), con(0), m(m) {}
    QueueItem(int type, Connection *con) : type(type), con(con), m(0) {}
    bool is_code() const {
      return type != -1;
    }
    int get_code () const {
      assert(is_code());
      return type;
    }
    Message *get_message() {
      assert(!is_code());
      return m.ptr();
    }
    Connection *get_connection() {
      assert(is_code());
      return con.ptr();
    }
  };

  class Queue {
  public:
    virtual ~Queue() {}
    virtual bool empty() const = 0;
    virtual unsigned length() const = 0;
    virtual void enqueue_strict(uint64_t cls, unsigned priority, QueueItem) = 0;
    virtual void enqueue(uint64_t cls, unsigned priority, unsigned cost, QueueItem) = 0;
    virtual void remove_by_class(uint64_t, std::list<QueueItem> *removed) = 0;
    virtual QueueItem dequeue() = 0;
  };

private:
  MsgrContext *pct_;
  Messenger *msgr_;
  mutable Mutex lock_;
  Cond cond_;

  Queue *mqueue_;

  std::set<std::pair<double, Message*> > marrival_;
  std::map<Message *, std::set<std::pair<double, Message*> >::iterator> marrival_map_;
  void add_arrival(Message *m) {
    marrival_map_.insert(
      std::make_pair(
        m,
        marrival_.insert(std::make_pair(m->get_recv_stamp(), m)).first
        )
      );
  }
  void remove_arrival(Message *m) {
    std::map<Message *, std::set<std::pair<double, Message*> >::iterator>::iterator i =
      marrival_map_.find(m);
    assert(i != marrival_map_.end());
    marrival_.erase(i->second);
    marrival_map_.erase(i);
  }

  uint64_t next_pipe_id;
    
  enum { D_CONNECT = 1, D_ACCEPT, D_BAD_REMOTE_RESET, D_BAD_RESET, D_NUM_CODES };

  /**
   * The DispatchThread runs dispatch_entry to empty out the dispatch_queue.
   */
  class DispatchThread : public Thread {
    DispatchQueue *dq;
  public:
    DispatchThread(DispatchQueue *dq) : dq(dq) {}
    void *entry() {
      Thread::set_name("dispatcher");
      dq->entry();
      return 0;
    }
  } dispatch_thread_;

  Mutex local_delivery_lock_;
  Cond local_delivery_cond_;
  bool stop_local_delivery_;
  std::list<std::pair<Message *, int> > local_messages_;
  class LocalDeliveryThread : public Thread {
    DispatchQueue *dq;
  public:
    LocalDeliveryThread(DispatchQueue *dq) : dq(dq) {}
    void *entry() {
      Thread::set_name("local dispatcher");
      dq->run_local_delivery();
      return 0;
    }
  } local_delivery_thread_;

  uint64_t pre_dispatch(Message *m);
  void post_dispatch(Message *m, uint64_t msize);

  void run_local_delivery();
  void entry();
public:
  bool stop_;

  void local_delivery(Message *m, int priority);

  double get_max_age(utime_t now) const;

  int get_queue_len() const {
    Mutex::Locker l(lock_);
    return mqueue_->length();
  }
    
  void queue_connect(Connection *con) {
    Mutex::Locker l(lock_);
    if (stop_)
      return;
    mqueue_->enqueue_strict(
      0,
      MSGR_MSG_PRIO_HIGHEST,
      QueueItem(D_CONNECT, con));
    cond_.Signal();
  }
  void queue_accept(Connection *con) {
    Mutex::Locker l(lock_);
    if (stop_)
      return;
    mqueue_->enqueue_strict(
      0,
      MSGR_MSG_PRIO_HIGHEST,
      QueueItem(D_ACCEPT, con));
    cond_.Signal();
  }
  void queue_remote_reset(Connection *con) {
    Mutex::Locker l(lock_);
    if (stop_)
      return;
    mqueue_->enqueue_strict(
      0,
      MSGR_MSG_PRIO_HIGHEST,
      QueueItem(D_BAD_REMOTE_RESET, con));
    cond_.Signal();
  }
  void queue_reset(Connection *con) {
    Mutex::Locker l(lock_);
    if (stop_)
      return;
    mqueue_->enqueue_strict(
      0,
      MSGR_MSG_PRIO_HIGHEST,
      QueueItem(D_BAD_RESET, con));
    cond_.Signal();
  }

  bool can_fast_dispatch(Message *m) const;
  void fast_dispatch(Message *m);
  void fast_preprocess(Message *m);
  void enqueue(Message *m, int priority, uint64_t id);
  void discard_queue(uint64_t id);
  uint64_t get_id() {
    Mutex::Locker l(lock_);
    return next_pipe_id++;
  }
  void start();
  void wait();
  void shutdown();
  bool is_started() const {return dispatch_thread_.is_started();}

  DispatchQueue(MsgrContext *pct, Messenger *msgr, Queue *q)
    : pct_(pct), msgr_(msgr),
      lock_("DispatchQueue::lock"),
      mqueue_(q),
      next_pipe_id(1),
      dispatch_thread_(this),
      local_delivery_lock_("DispatchQueue::local_delivery_lock"),
      stop_local_delivery_(false),
      local_delivery_thread_(this),
      stop_(false)
    {}
  ~DispatchQueue() {
    delete mqueue_;
  }
};

} //namespace msgr
#endif
