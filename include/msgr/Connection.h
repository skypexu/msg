#ifndef MSGR_CONNECTION_61217ad8106a_H
#define MSGR_CONNECTION_61217ad8106a_H

#include <stdlib.h>
#include <ostream>

#include "intrusive_ptr.h"
#include "msgr_assert.h"

#include "msg_types.h"
#include "utime.h"
#include "buffer.h"
#include "Mutex.h"

#include "RefCountedObj.h"

#include "dout.h"
#include "MsgrConfig.h"

namespace msgr {

// ======================================================

// abstract Connection, for keeping per-connection state

class Message;
class Messenger;

class Connection : public RefCountedObject {
protected:
  Mutex lock;
  Messenger *msgr;
  RefCountedObject *priv;
  int peer_type;
  entity_addr_t peer_addr;
  utime_t last_keepalive_ack;

  /* hack for memory utilization debugging. */
  static void inc_total_alloc();
  static void dec_total_alloc();

  static void inc_total_local_alloc();
  static void dec_total_local_alloc();

  uint64_t features;

  int rx_buffers_version;
  std::map<msgr_tid_t, std::pair<bufferlist,int> > rx_buffers;

  friend class PipeConnection;

public:
  bool failed; // true if we are a lossy connection that has failed.

  static unsigned int get_total_alloc();
  static unsigned int get_total_local_alloc();

  Connection(MsgrContext *pct, Messenger *m);

  virtual ~Connection();

  void set_priv(RefCountedObject *o) {
    RefCountedObject *old;
    {
        Mutex::Locker l(lock);
        old = priv;
        priv = o;
    }
    if (old)
      old->put();
  }

  RefCountedObject *get_priv() {
    Mutex::Locker l(lock);
    if (priv)
      return priv->get();
    return NULL;
  }

  /**
   * Used to judge whether this connection is ready to send. Usually, the
   * implementation need to build a own shakehand or sesson then it can be
   * ready to send.
   *
   * @return true if ready to send, or false otherwise
   */
  virtual bool is_connected() = 0;

  Messenger *get_messenger() {
    return msgr;
  }

  /**
   * Queue the given Message to send out on the given Connection.
   * Success in this function does not guarantee Message delivery, only
   * success in queueing the Message. Other guarantees may be provided based
   * on the Connection policy.
   *
   * @param m The Message to send. The Messenger consumes a single reference
   * when you pass it in.
   * @param con The Connection to send the Message out on.
   *
   * @return 0 on success, or -errno on failure.
   */
  virtual int send_message(Message *m) = 0;
  /**
   * Send a "keepalive" ping along the given Connection, if it's working.
   * If the underlying connection has broken, this function does nothing.
   *
   * @return 0, or implementation-defined error numbers.
   */
  virtual void send_keepalive() = 0;
  /**
   * Mark down the given Connection.
   *
   * This will cause us to discard its outgoing queue, and if reset
   * detection is enabled in the policy and the endpoint tries to
   * reconnect they will discard their queue when we inform them of
   * the session reset.
   *
   * It does not generate any notifications to the Dispatcher.
   */
  virtual void mark_down() = 0;

  /**
   * Mark a Connection as "disposable", setting it to lossy
   * (regardless of initial Policy).  This does not immediately close
   * the Connection once Messages have been delivered, so as long as
   * there are no errors you can continue to receive responses; but it
   * will not attempt to reconnect for message delivery or preserve
   * your old delivery semantics, either.
   *
   * TODO: There's some odd stuff going on in our SimpleMessenger
   * implementation during connect that looks unused; is there
   * more of a contract that that's enforcing?
   */
  virtual void mark_disposable() = 0;


  int get_peer_type() const { return peer_type; }
  void set_peer_type(int t) { peer_type = t; }

  bool peer_is_mon() const { return peer_type == MSGR_ENTITY_TYPE_MON; }
  bool peer_is_mds() const { return peer_type == MSGR_ENTITY_TYPE_MDS; }
  bool peer_is_osd() const { return peer_type == MSGR_ENTITY_TYPE_OSD; }
  bool peer_is_client() const { return peer_type == MSGR_ENTITY_TYPE_CLIENT; }

  const entity_addr_t& get_peer_addr() const { return peer_addr; }
  void set_peer_addr(const entity_addr_t& a) { peer_addr = a; }

  uint64_t get_features() const { return features; }
  bool has_feature(uint64_t f) const { return features & f; }
  void set_features(uint64_t f) { features = f; }
  void set_feature(uint64_t f) { features |= f; }

  void post_rx_buffer(msgr_tid_t tid, bufferlist& bl) {
    Mutex::Locker l(lock);
    ++rx_buffers_version;
    rx_buffers[tid] = std::pair<bufferlist,int>(bl, rx_buffers_version);
  }

  void revoke_rx_buffer(msgr_tid_t tid) {
    Mutex::Locker l(lock);
    rx_buffers.erase(tid);
  }

  utime_t get_last_keepalive_ack() const {
    return last_keepalive_ack;
  }

  virtual void dump(std::ostream &out);

  static void for_each(bool (*cb)(Connection *, void *), void *arg);

  static void dump_all(std::ostream &out);
};

typedef intrusive_ptr<Connection> ConnectionRef;

} // namespace msgr

#endif // MSGR_CONNECTION_61217ad8106a_H
