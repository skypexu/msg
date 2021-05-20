// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Msgr - a messenger
 *
 * Author: Xu Yifeng <yifeng.xyf@alibaba-inc.com>
 */
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef MSGR_MSG_ASYNCCONNECTION_H
#define MSGR_MSG_ASYNCCONNECTION_H

#include <pthread.h>
#include <climits>
#include <unistd.h>

#include <ev++.h>

#include "msgr/Auth.h"
#include "msgr/Mutex.h"
#include "msgr/buffer.h"
#include "msgr/Connection.h"
#include "msgr/Messenger.h"

#include "Event.h"
#include "net_handler.h"

namespace msgr {

class AsyncMessenger;
class DispatchQueue;
class AsyncConnection;

typedef intrusive_ptr<AsyncConnection> AsyncConnectionRef;

/*
 * AsyncConnection maintains a logic session between two endpoints. In other
 * word, a pair of addresses can find the only AsyncConnection. AsyncConnection
 * will handle with network fault or read/write transactions. If one file
 * descriptor broken, AsyncConnection will maintain the message queue and
 * sequence, try to reconnect peer endpoint.
 */
class AsyncConnection : public Connection {

  int read_bulk(int fd, char *buf, uint32_t len);
  int do_sendmsg(struct msghdr &msg, uint32_t len, bool more);
  // if "send" is false, it will only append bl to send buffer
  // the main usage is avoid error happen outside messenger threads
  int _try_send(bufferlist &bl, bool send=true, bool allow_unlock = false);
  int _send(Message *m);
  int read_until(uint32_t needed, char *p);
  int _process_connection();
  void _connect();
  void _stop();
  void kick_write();
  void start_write(bool feed=false);
  void start_write_locked(bool feed=false);
  int handle_connect_reply(msgr_msg_connect &connect, msgr_msg_connect_reply &r);
  int handle_connect_msg(msgr_msg_connect &m, bufferlist &aubl, bufferlist &bl);
  void was_session_reset();
  void fault();
  void discard_out_queue();
  void discard_requeued_up_to(uint64_t seq);
  void requeue_sent();
  int randomize_out_seq();
  void handle_ack(uint64_t seq);
  int _send_keepalive_or_ack(bool ack=false, utime_t *t=NULL);
  int write_message(msgr_msg_header& header, msgr_msg_footer& footer, bufferlist& blist);
  int _reply_accept(char tag, msgr_msg_connect &connect, msgr_msg_connect_reply &reply,
                    bufferlist authorizer_reply);
  bool is_queued() {
    return !out_q.empty() || outcoming_bl.length();
  }
  Message *_get_next_outgoing() {
    Message *m = 0;
    while (!m && !out_q.empty()) {
      std::map<int, std::deque<Message*> >::reverse_iterator p = out_q.rbegin();
      if (!p->second.empty()) {
        m = p->second.front();
        p->second.pop_front();
      }
      if (p->second.empty())
        out_q.erase(p->first);
    }
    return m;
  }

  void schedule_timer(ev::timer &timer, double timeo);
  void schedule_timer_locked(ev::timer &timer, double timeo);
  void release_throttle(int state);
  void cleanup_handler();
  void wait_sending();

  // Only call when AsyncConnection first construct
  void connect(const entity_addr_t& addr, int type) {
    lock.Lock();
    set_peer_type(type);
    set_peer_addr(addr);
    policy = msgr->get_policy(type);
    _connect();
    lock.Unlock();
  }
  // Only call when AsyncConnection first construct
  void accept(int sd);

  static void close_socket(int fd) {
    if (fd >= 0) {
      ::shutdown(fd, SHUT_RDWR);
      ::close(fd);
    }
  }

  friend class AsyncMessenger;

 public:
  AsyncConnection(MsgrContext *cct, AsyncMessenger *m, EventCenter *c);
  ~AsyncConnection() override;

  std::ostream& _conn_prefix(std::ostream *_dout);

  virtual bool is_connected() override {
    Mutex::Locker l(lock);
    return state >= STATE_OPEN && state <= STATE_OPEN_TAG_CLOSE;
  }
  virtual int send_message(Message *m) override;
  virtual void send_keepalive() override;
  virtual void mark_down() override;
  virtual void mark_disposable() override;

 private:
  enum {
    STATE_NONE,
    STATE_OPEN,
    STATE_OPEN_KEEPALIVE2,
    STATE_OPEN_KEEPALIVE2_ACK,
    STATE_OPEN_TAG_ACK,
    STATE_OPEN_MESSAGE_HEADER,
    STATE_OPEN_MESSAGE_THROTTLE_MESSAGE,
    STATE_OPEN_MESSAGE_THROTTLE_BYTES,
    STATE_OPEN_MESSAGE_READ_FRONT,
    STATE_OPEN_MESSAGE_READ_MIDDLE,
    STATE_OPEN_MESSAGE_READ_DATA_PREPARE,
    STATE_OPEN_MESSAGE_READ_DATA,
    STATE_OPEN_MESSAGE_READ_FOOTER,
    STATE_OPEN_MESSAGE_THROTTLE_DISPATCH_SIZE,
    STATE_OPEN_MESSAGE_DISPATCH,
    STATE_OPEN_TAG_CLOSE,
    STATE_WAIT_SEND,
    STATE_CONNECTING,
    STATE_CONNECTING_WAIT_BANNER,
    STATE_CONNECTING_WAIT_IDENTIFY_PEER,
    STATE_CONNECTING_SEND_CONNECT_MSG,
    STATE_CONNECTING_WAIT_CONNECT_REPLY,
    STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH,
    STATE_CONNECTING_WAIT_ACK_SEQ,
    STATE_CONNECTING_READY,
    STATE_ACCEPTING,
    STATE_ACCEPTING_WAIT_BANNER_ADDR,
    STATE_ACCEPTING_WAIT_CONNECT_MSG,
    STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH,
    STATE_ACCEPTING_WAIT_SEQ,
    STATE_ACCEPTING_READY,
    STATE_STANDBY,
    STATE_CLOSED,
    STATE_WAIT,       // just wait for racing connection
  };

  static const int TCP_PREFETCH_MIN_SIZE;
  static const char *get_state_name(int state) {
      const char* const statenames[] = {"STATE_NONE",
                                        "STATE_OPEN",
                                        "STATE_OPEN_KEEPALIVE2",
                                        "STATE_OPEN_KEEPALIVE2_ACK",
                                        "STATE_OPEN_TAG_ACK",
                                        "STATE_OPEN_MESSAGE_HEADER",
                                        "STATE_OPEN_MESSAGE_THROTTLE_MESSAGE",
                                        "STATE_OPEN_MESSAGE_THROTTLE_BYTES",
                                        "STATE_OPEN_MESSAGE_READ_FRONT",
                                        "STATE_OPEN_MESSAGE_READ_MIDDLE",
                                        "STATE_OPEN_MESSAGE_READ_DATA_PREPARE",
                                        "STATE_OPEN_MESSAGE_READ_DATA",
                                        "STATE_OPEN_MESSAGE_READ_FOOTER",
                                        "STATE_OPEN_MESSAGE_THROTTLE_DISPATCH_SIZE",
                                        "STATE_OPEN_MESSAGE_DISPATCH",
                                        "STATE_OPEN_TAG_CLOSE",
                                        "STATE_WAIT_SEND",
                                        "STATE_CONNECTING",
                                        "STATE_CONNECTING_WAIT_BANNER",
                                        "STATE_CONNECTING_WAIT_IDENTIFY_PEER",
                                        "STATE_CONNECTING_SEND_CONNECT_MSG",
                                        "STATE_CONNECTING_WAIT_CONNECT_REPLY",
                                        "STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH",
                                        "STATE_CONNECTING_WAIT_ACK_SEQ",
                                        "STATE_CONNECTING_READY",
                                        "STATE_ACCEPTING",
                                        "STATE_ACCEPTING_WAIT_BANNER_ADDR",
                                        "STATE_ACCEPTING_WAIT_CONNECT_MSG",
                                        "STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH",
                                        "STATE_ACCEPTING_WAIT_SEQ",
                                        "STATE_ACCEPTING_READY",
                                        "STATE_STANDBY",
                                        "STATE_CLOSED",
                                        "STATE_WAIT"};
      return statenames[state];
  }

  MsgrContext *cc;
  AsyncMessenger *async_msgr;
  int global_seq;
  uint32_t connect_seq, peer_global_seq;
  uint64_t out_seq;
  uint64_t in_seq, in_seq_acked;
  int state;
  int state_after_send;
  int sd;
  int port;
  Messenger::Policy policy;
  std::map<int, std::deque<Message*> > out_q;  // priority queue for outbound msgs
  std::deque<Message*> sent;
  Mutex lock;
  utime_t backoff;         // backoff time
  bool keepalive;
  bool keepalive_ack;
  utime_t keepalive_ack_stamp;
  struct iovec msgvec[IOV_MAX];
  char *recv_buf;
  uint32_t recv_max_prefetch;
  uint32_t recv_start;
  uint32_t recv_end;
  Cond cond;
  std::set<uint64_t> register_time_events; // need to delete it if stop

  // Tis section are temp variables used by state transition

  // Open state
  utime_t recv_stamp;
  utime_t throttle_stamp;
  uint32_t msg_left;
  msgr_msg_header current_header;
  msgr_msg_footer current_footer;
  bufferlist data_buf;
  bufferlist::iterator data_blp;
  bufferlist front, middle, data;
  msgr_msg_connect connect_msg;
  // Connecting state
  bool is_reset_from_peer;
  bool got_bad_auth;
  AuthAuthorizer *authorizer;
  msgr_msg_connect_reply connect_reply;
  // Accepting state
  entity_addr_t socket_addr;
  CryptoKey session_key;
  int replacing;    // when replacing process happened, we will reply connect
                     // side with RETRY tag and accept side will clear replaced
                     // connection. So when connect side reissue connect_msg,
                     // there won't exists conflicting connection so we use
                     // "replacing" to skip RESETSESSION to avoid detect wrong
                     // presentation
  atomic_t stopping;

  // used only for local state, it will be overwrite when state transition
  char *state_buffer;
  // used only by "read_until"
  unsigned state_offset;
  bufferlist outcoming_bl;
  NetHandler net;
  EventCenter *center;
  std::shared_ptr<AuthSessionHandler> session_security;
  ev::timer ev_process, ev_replace;
  ev::io ev_read, ev_write;
  int replacing_sd;
  msgr_msg_connect replacing_connect;
  bufferlist replacing_authorizer_reply;
  std::vector<char> replacing_buf;
  bool replacing_reset_session;
  DispatchQueue *in_q;
  bool queue_reset;
  uint64_t conn_id;
  bool sending, want_send, handle_write_delayed;

  void process();
  void do_process();
  void handle_write();
  void handle_process();
  void handle_wakeup();
  void _release();
  void handle_replace();
  void handle_stop();
  void do_replace();
  void mark_down(bool post_event, bool wait);

  class C_stop : public EventCallback {
    AsyncConnectionRef conn_;
    public:
      C_stop(AsyncConnection *conn): conn_(conn) {}
      void do_request(int id);
  };

  class C_release : public EventCallback {
    AsyncConnection *conn_;
    public:
      C_release(AsyncConnection *conn): conn_(conn) {}
      void do_request(int id);
  };

 public:
  // Used by msgr's mark_down_all
  void stop();

 // used by Connection::dump_all()
  virtual void dump(std::ostream &os) override;
}; /* AsyncConnection */

} // namespace msgr
#endif
