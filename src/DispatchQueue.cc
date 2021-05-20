#include "msgr/Message.h"
#include "msgr/Messenger.h"
#include "msgr/MsgrContext.h"
#include "msgr/Clock.h"
#include "msgr/dout.h"

#include "DispatchQueue.h"

#define dout_subsys msgr_subsys_ms

using namespace std;

namespace msgr {

/*******************
 * DispatchQueue
 */

#undef dout_prefix
#define dout_prefix *dout_ << "-- " << msgr_->get_myaddr() << " "

double DispatchQueue::get_max_age(utime_t now) const {
  Mutex::Locker l(lock_);
  if (marrival_.empty())
    return 0;
  else
    return (now - marrival_.begin()->first);
}

uint64_t DispatchQueue::pre_dispatch(Message *m)
{
  ldout(pct_,1) << "<== " << m->get_source_inst()
               << " " << m->get_seq()
               << " ==== " << *m
               << " ==== " << m->get_payload().length()
               << "+" << m->get_middle().length()
               << "+" << m->get_data().length()
               << " (" << m->get_footer().front_crc << " "
               << m->get_footer().middle_crc
               << " " << m->get_footer().data_crc << ")"
               << " " << m << " con " << m->get_connection()
               << dendl;
  uint64_t msize = m->get_dispatch_throttle_size();
  m->set_dispatch_throttle_size(0); // clear it out, in case we requeue this message.
  return msize;
}

void DispatchQueue::post_dispatch(Message *m, uint64_t msize)
{
  msgr_->dispatch_throttle_release(msize);
  ldout(pct_,20) << "done calling dispatch on " << m << dendl;
}

bool DispatchQueue::can_fast_dispatch(Message *m) const
{
  return msgr_->ms_can_fast_dispatch(m);
}

void DispatchQueue::fast_dispatch(Message *m)
{
  uint64_t msize = pre_dispatch(m);
  msgr_->ms_fast_dispatch(m);
  post_dispatch(m, msize);
}

void DispatchQueue::fast_preprocess(Message *m)
{
  msgr_->ms_fast_preprocess(m);
}

void DispatchQueue::enqueue(Message *m, int priority, uint64_t id)
{
  Mutex::Locker l(lock_);

  ldout(pct_,20) << "queue " << m << " prio " << priority << dendl;
  if (mqueue_->empty())
    cond_.Signal();
  add_arrival(m);
  if (priority >= MSGR_MSG_PRIO_LOW) {
    mqueue_->enqueue_strict(
        id, priority, QueueItem(m));
  } else {
    mqueue_->enqueue(
        id, priority, m->get_cost(), QueueItem(m));
  }
}

void DispatchQueue::local_delivery(Message *m, int priority)
{
  m->set_connection(msgr_->get_loopback_connection());
  m->set_recv_stamp(msgr_clock_now(pct_));
  Mutex::Locker l(local_delivery_lock_);
  if (local_messages_.empty())
    local_delivery_cond_.Signal();
  local_messages_.push_back(std::make_pair(m, priority));
  return;
}

void DispatchQueue::run_local_delivery()
{
  Mutex::Locker locker(local_delivery_lock_);
  while (true) {
    while (!local_messages_.empty()) {
      pair<Message *, int> mp = local_messages_.front();
      local_messages_.pop_front();
      Mutex::Unlocker unlocker(local_delivery_lock_);

      Message *m = mp.first;
      int priority = mp.second;
      fast_preprocess(m);
      if (can_fast_dispatch(m)) {
        fast_dispatch(m);
      } else {
        enqueue(m, priority, 0);
      }
    }
    if (stop_local_delivery_)
        break;
    local_delivery_cond_.Wait(local_delivery_lock_);
  }
}

/*
 * This function delivers incoming messages to the Messenger.
 * Pipes with messages are kept in queues; when beginning a message
 * delivery the highest-priority queue is selected, the pipe from the
 * front of the queue is removed, and its message read. If the pipe
 * has remaining messages at that priority level, it is re-placed on to the
 * end of the queue. If the queue is empty; it's removed.
 * The message is then delivered and the process starts again.
 */
void DispatchQueue::entry()
{
  Mutex::Locker locker(lock_);
  while (true) {
    while (!mqueue_->empty()) {
      QueueItem qitem = mqueue_->dequeue();
      if (!qitem.is_code())
        remove_arrival(qitem.get_message());

      Mutex::Unlocker ul(lock_);

      if (qitem.is_code()) {
        switch (qitem.get_code()) {
        case D_BAD_REMOTE_RESET:
          msgr_->ms_deliver_handle_remote_reset(qitem.get_connection());
          break;
        case D_CONNECT:
          msgr_->ms_deliver_handle_connect(qitem.get_connection());
          break;
        case D_ACCEPT:
          msgr_->ms_deliver_handle_accept(qitem.get_connection());
          break;
        case D_BAD_RESET:
          msgr_->ms_deliver_handle_reset(qitem.get_connection());
          break;
        default:
          assert(0);
        }
      } else {
        Message *m = qitem.get_message();
        if (stop_) {
          ldout(pct_,10) << " stop flag set, discarding " << m << " " << *m << dendl;
          m->put();
        } else {
          uint64_t msize = pre_dispatch(m);
          msgr_->ms_deliver_dispatch(m);
          post_dispatch(m, msize);
        }
      }

    }
    if (stop_)
      break;

    // wait for something to be put on queue
    cond_.Wait(lock_);
  }
}

void DispatchQueue::discard_queue(uint64_t id) {
  Mutex::Locker l(lock_);
  std::list<QueueItem> removed;
  mqueue_->remove_by_class(id, &removed);
  for (list<QueueItem>::iterator i = removed.begin();
       i != removed.end();
       ++i) {
    assert(!(i->is_code())); // We don't discard id 0, ever!
    Message *m = i->get_message();
    remove_arrival(m);
    msgr_->dispatch_throttle_release(m->get_dispatch_throttle_size());
    m->put();
  }
}

void DispatchQueue::start()
{
  assert(!stop_);
  assert(!dispatch_thread_.is_started());
  dispatch_thread_.create();
  local_delivery_thread_.create();
}

void DispatchQueue::wait()
{
  dispatch_thread_.join();
  stop_ = false;
}

void DispatchQueue::shutdown()
{
  // stop my local delivery thread
  local_delivery_lock_.Lock();
  stop_local_delivery_ = true;
  local_delivery_cond_.Signal();
  local_delivery_lock_.Unlock();
  local_delivery_thread_.join();
  stop_local_delivery_ = false;

  // stop my dispatch thread
  lock_.Lock();
  stop_ = true;
  cond_.Signal();
  lock_.Unlock();
}

} //namespace msgr
