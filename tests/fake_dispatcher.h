#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fstream>

#include "msgr/Mutex.h"
#include "msgr/Cond.h"
#include "msgr/Dispatcher.h"
#include "msgr/msg_types.h"
#include "msgr/Message.h"
#include "msgr/Messenger.h"
#include "msgr/Connection.h"

#include "MPing.h"
#include "MCommand.h"

class FakeDispatcher : public Dispatcher {
 public:
  struct Session : public RefCountedObject {
    Mutex lock;
    uint64_t count;
    ConnectionRef con;

    Session(ConnectionRef c): RefCountedObject(g_context), lock("FakeDispatcher::Session::lock"), count(0), con(c) {
    }
    uint64_t get_count() { return count; }
  };

  Mutex lock;
  Cond cond;
  bool is_server;
  bool got_new;
  bool got_remote_reset;
  bool got_connect;

  FakeDispatcher(bool s): Dispatcher(g_context), lock("FakeDispatcher::lock"),
                          is_server(s), got_new(false), got_remote_reset(false),
                          got_connect(false) {}
  bool ms_can_fast_dispatch_any() const { return true; }
  bool ms_can_fast_dispatch(Message *m) const {
    switch (m->get_type()) {
    case TEST_MSG_PING:
      return true;
    default:
      return false;
    }
  }

  const char *side() { return is_server ? "server: " : "client: "; }
  void ms_handle_fast_connect(Connection *con) {
    lock.Lock();
    ldout(g_context, 0) << side() << __func__ << " " << con << dendl;
    Session *s = static_cast<Session*>(con->get_priv());
    if (!s) {
      s = new Session(con);
      con->set_priv(s->get());
      ldout(g_context, 0) << side() << __func__ << " con: " << con << " count: " << s->count << dendl;
    }
    s->put();
    got_connect = true;
    cond.Signal();
    lock.Unlock();
  }
  void ms_handle_fast_accept(Connection *con) {
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << " " << con << dendl;
    Session *s = static_cast<Session*>(con->get_priv());
    if (!s) {
      s = new Session(con);
      con->set_priv(s->get());
    }
    s->put();
  }
  bool ms_dispatch(Message *m) {
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << dendl;
    Session *s = static_cast<Session*>(m->get_connection()->get_priv());
    if (!s) {
      s = new Session(m->get_connection());
      m->get_connection()->set_priv(s->get());
    }
    s->put();
    Mutex::Locker l1(s->lock);
    s->count++;
    ldout(g_context, 0) << side()  <<  __func__ << " conn: " << m->get_connection() << " session " << s << " count: " << s->count << dendl;
    if (is_server) {
      reply_message(m);
    }
    got_new = true;
    cond.Signal();
    m->put();
    return true;
  }
  bool ms_handle_reset(Connection *con) {
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << " " << con << dendl;
    Session *s = static_cast<Session*>(con->get_priv());
    if (s) {
      s->con.reset(NULL);  // break con <-> session ref cycle
      con->set_priv(NULL);   // break ref <-> session cycle, if any
      s->put();
    }
    return true;
  }
  void ms_handle_remote_reset(Connection *con) {
#if 0
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << " " << con << dendl;
    Session *s = static_cast<Session*>(con->get_priv());
    if (s) {
      s->con.reset(NULL);  // break con <-> session ref cycle
      con->set_priv(NULL);   // break ref <-> session cycle, if any
      s->put();
    }
    got_remote_reset = true;
#endif
  }
  void ms_handle_fast_remote_reset(Connection *con) {
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << " " << con << dendl;
    Session *s = static_cast<Session*>(con->get_priv());
    if (s) {
      s->con.reset(NULL);  // break con <-> session ref cycle
      con->set_priv(NULL);   // break ref <-> session cycle, if any
      s->put();
    }
    got_remote_reset = true;
  }
  void ms_fast_dispatch(Message *m) {
    Mutex::Locker l(lock);
    ldout(g_context, 0) << side() << __func__ << dendl;
    Session *s = static_cast<Session*>(m->get_connection()->get_priv());
    if (!s) {
      s = new Session(m->get_connection());
      m->get_connection()->set_priv(s->get());
    }
    s->put();
    Mutex::Locker l1(s->lock);
    s->count++;
    ldout(g_context, 0) << side() << __func__ << " conn: " << m->get_connection() << " session " << s << " count: " << s->count << dendl;
    if (is_server) {
      reply_message(m);
    }
    got_new = true;
    cond.Signal();
    m->put();
  }

  bool ms_verify_authorizer(Connection *con, int peer_type, int protocol,
                            bufferlist& authorizer, bufferlist& authorizer_reply,
                            bool& isvalid, CryptoKey& session_key) {
    isvalid = true;
    return true;
  }

  void reply_message(Message *m) {
    MPing *rm = new MPing();
    m->get_connection()->send_message(rm);
  }
};

typedef FakeDispatcher::Session Session;

