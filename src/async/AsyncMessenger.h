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

#ifndef MSGR_ASYNCMESSENGER_H
#define MSGR_ASYNCMESSENGER_H

#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ev++.h>

#include "msgr/msgr_assert.h"
#include "msgr/types.h"
#include "msgr/Mutex.h"
#include "msgr/atomic.h"
#include "msgr/Cond.h"
#include "msgr/Thread.h"
#include "msgr/Throttle.h"
#include "msgr/SimplePolicyMessenger.h"

#include "AsyncConnection.h"
#include "LocalConnection.h"
#include "net_handler.h"
#include "../DispatchQueue.h"

namespace msgr {

class AsyncMessenger;
class WorkerPool;

class Worker : public Thread {
  static const uint64_t InitEventNumber = 5000;
  static const uint64_t EventMaxWaitUs = 30000000;
  MsgrContext *cct;
  WorkerPool *pool;
  bool done;
  int id;

 public:
  EventCenter center;

  Worker(MsgrContext *c, WorkerPool *p, int i)
    : cct(c), pool(p), done(false), id(i), center(c) {
  }
  void *entry();
  void stop();
};

/**
 * If the Messenger binds to a specific address, the Processor runs
 * and listens for incoming connections.
 */
class Processor {
  AsyncMessenger *msgr;
  NetHandler net;
  Worker *worker;
  int listen_sd;
  uint64_t nonce;
  ev::io ev_read;
  bool stopped;
  Mutex lock;
  Cond cond;

  class C_ProcessorStop : public EventCallback {
    Processor *p;
    public:
      C_ProcessorStop(Processor *proc): p(proc) {}
      void do_request(int id) {
        p->do_stop();
      }
  };

  void do_stop();

 public:
  Processor(AsyncMessenger *r, MsgrContext *c, uint64_t n): msgr(r), net(c),
     worker(NULL), listen_sd(-1), nonce(n), stopped(false),
     lock("AsyncMessenger::Processor::lock")
     {}
  ~Processor() { assert(NULL == worker); }

  void stop();
  int bind(const entity_addr_t &bind_addr, const std::set<int>& avoid_ports);
  int rebind(const std::set<int>& avoid_port);
  int start(Worker *w);
  void accept();
};

class WorkerPool: MsgrContext::AssociatedSingletonObject {
  WorkerPool(const WorkerPool &);
  WorkerPool& operator=(const WorkerPool &);
  MsgrContext *cct;
  atomic_count<unsigned> seq;
  std::vector<Worker*> workers;
  std::vector<int> coreids;
  // Used to indicate whether thread started
  bool started;
  Mutex barrier_lock;
  Cond barrier_cond;
  atomic_t barrier_count;

  class C_barrier : public EventCallback {
    WorkerPool *pool;
   public:
    C_barrier(WorkerPool *p): pool(p) {}
    void do_request(int id) {
      Mutex::Locker l(pool->barrier_lock);
      pool->barrier_count.dec();
      pool->barrier_cond.Signal();
    }
  };
  friend class C_barrier;
 public:
  WorkerPool(MsgrContext *c);
  virtual ~WorkerPool();
  void start();
  void stop();
  Worker *get_worker() {
    return workers[seq.inc()%workers.size()];
  }
  int get_cpuid(int id) {
    if (coreids.empty())
      return -1;
    return coreids[id % coreids.size()];
  }
  void barrier();
  // uniq name for CephContext to distinguish differnt object
  static const std::string name;
};

/*
 * AsyncMessenger is represented for maintaining a set of asynchronous connections,
 * it may own a bind address and the accepted connections will be managed by
 * AsyncMessenger.
 *
 */

class AsyncMessenger : public SimplePolicyMessenger {
  // First we have the public Messenger interface implementation...
public:
  /**
   * Initialize the AsyncMessenger!
   *
   * @param cct The CephContext to use
   * @param name The name to assign ourselves
   * _nonce A unique ID to use for this AsyncMessenger. It should not
   * be a value that will be repeated if the daemon restarts.
   */
  AsyncMessenger(MsgrContext *cct, entity_name_t name,
                 std::string mname, uint64_t _nonce);

  /**
   * Destroy the AsyncMessenger. Pretty simple since all the work is done
   * elsewhere.
   */
  virtual ~AsyncMessenger() override;

  /** @defgroup Accessors
   * @{
   */
  void set_addr_unknowns(entity_addr_t& addr);

  bool get_need_addr() const { return need_addr; }

  virtual int get_dispatch_queue_len() override {
    return dispatch_queue.get_queue_len();
  }

  virtual double get_dispatch_queue_max_age(utime_t now) override {
    return dispatch_queue.get_max_age(now);
  }

  /** @} Accessors */

  /**
   * @defgroup Configuration functions
   * @{
   */
  void set_cluster_protocol(int p) {
    assert(!started && !did_bind);
    cluster_protocol = p;
  }

  virtual int bind(const entity_addr_t& bind_addr) override;
  virtual int rebind(const std::set<int>& avoid_ports) override;
  virtual void unbind() override;

  /** @} Configuration functions */

  /**
   * @defgroup Startup/Shutdown
   * @{
   */
  virtual int start() override;
  virtual void wait() override;
  virtual int shutdown() override;

  /** @} // Startup/Shutdown */

  /**
   * @defgroup Messaging
   * @{
   */

  /** @} // Messaging */

  /**
   * @defgroup Connection Management
   * @{
   */
  virtual ConnectionRef get_connection(const entity_inst_t& dest) override;
  virtual ConnectionRef get_loopback_connection() override;
  virtual void mark_down_all() override;
  /** @} // Connection Management */

  virtual void dispatch_throttle_release(uint64_t ) override {}

  /**
   * @defgroup Inner classes
   * @{
   */

  Connection *create_anon_connection() {
    Mutex::Locker l(lock);
    Worker *w = pool->get_worker();
    return new AsyncConnection(cct, this, &w->center);
  }

  /**
   * @} // Inner classes
   */

protected:
  /**
   * @defgroup Messenger Interfaces
   * @{
   */
  /**
   * Start up the DispatchQueue thread once we have somebody to dispatch to.
   */
  virtual void ready() override;
  /** @} // Messenger Interfaces */

private:

  /**
   * @defgroup Utility functions
   * @{
   */

  /**
   * Create a connection associated with the given entity (of the given type).
   * Initiate the connection. (This function returning does not guarantee
   * connection success.)
   *
   * @param addr The address of the entity to connect to.
   * @param type The peer type of the entity at the address.
   * @param con An existing Connection to associate with the new connection. If
   * NULL, it creates a new Connection.
   * @param msg an initial message to queue on the new connection
   *
   * @return a pointer to the newly-created connection. Caller does not own a
   * reference; take one if you need it.
   */
  AsyncConnectionRef create_connect(const entity_addr_t& addr, int type);

private:
  friend class Processor;
  friend class AsyncConnection;
  friend class LocalConnection;

  /// overall lock used for AsyncMessenger data structures
  Mutex lock;
  WorkerPool *pool;

  // AsyncMessenger stuff
  /// approximately unique ID set by the Constructor for use in entity_addr_t
  uint64_t nonce;

  /// true, specifying we haven't learned our addr; set false when we find it.
  // maybe this should be protected by the lock?
  bool need_addr;

  /**
   *  The following aren't lock-protected since you shouldn't be able to race
   *  the only writers.
   */

  /**
   *  false; set to true if the AsyncMessenger bound to a specific address;
   *  and set false again by Accepter::stop().
   */
  bool did_bind;

  /// counter for the global seq our connection protocol uses
  uint32_t global_seq;
  /// lock to protect the global_seq
  Spinlock global_seq_lock;

  /**
   * hash map of addresses to Asyncconnection
   *
   * NOTE: a Asyncconnection* with state CLOSED may still be in the map but is considered
   * invalid and can be replaced by anyone holding the msgr lock
   */
  std::unordered_map<entity_addr_t, AsyncConnectionRef> conns;

  /**
   * list of connection are in teh process of accepting
   *
   * These are not yet in the conns map.
   */
  // FIXME clear up
  std::set<AsyncConnectionRef> accepting_conns;

  /// internal cluster protocol version, if any, for talking to entities of the same type.
  int cluster_protocol;

  Processor processor;

  DispatchQueue dispatch_queue;

  /// con used for sending messages to ourselves
  ConnectionRef local_connection;

  /// lock for start()/shutdown()/wait()
  Mutex start_lock;

  /// whether shutdown() is called
  bool shutdown_called;

  AsyncConnectionRef _lookup_conn(const entity_addr_t& k) {
    assert(lock.is_locked());
    std::unordered_map<entity_addr_t, AsyncConnectionRef>::iterator p = conns.find(k);
    if (p == conns.end())
      return NULL;
    return p->second;
  }

  /**
   * Unregister connection from `conns`
   */
  void _unregister_conn(AsyncConnectionRef conn) {
    assert(lock.is_locked());
    if (conns.count(conn->peer_addr)) {
      AsyncConnectionRef existing = conns[conn->peer_addr];
      if (conn == existing) // this might be a replacing conn ?!
        conns.erase(conn->peer_addr);
    }
    accepting_conns.erase(conn);
  }

  int _accept_conn(AsyncConnectionRef conn) {
    assert(lock.is_locked());
    if (conns.count(conn->peer_addr)) {
      AsyncConnectionRef existing = conns[conn->peer_addr];
      if (conn != existing)
        return -1;
    }
    conns[conn->peer_addr] = conn;
    accepting_conns.erase(conn);
    return 0;
  }

public:
  /**
   * @defgroup AsyncMessenger internals
   * @{
   */
  /**
   * This wraps _lookup_conn.
   */
  AsyncConnectionRef lookup_conn(const entity_addr_t& k) {
    Mutex::Locker l(lock);
    return _lookup_conn(k);
  }

  void learned_addr(const entity_addr_t &peer_addr_for_me);
  AsyncConnectionRef add_accept(int sd);

  /**
   * This wraps ms_deliver_get_authorizer. We use it for AsyncConnection.
   */
  AuthAuthorizer *get_authorizer(int peer_type, bool force_new) {
    return ms_deliver_get_authorizer(peer_type, force_new);
  }

  /**
   * This wraps ms_deliver_verify_authorizer; we use it for AsyncConnection.
   */
  bool verify_authorizer(Connection *con, int peer_type, int protocol, bufferlist& auth, bufferlist& auth_reply,
                         bool& isvalid, CryptoKey& session_key) {
    return ms_deliver_verify_authorizer(con, peer_type, protocol, auth,
                                        auth_reply, isvalid, session_key);
  }
  /**
   * Increment the global sequence for this AsyncMessenger and return it.
   * This is for the connect protocol, although it doesn't hurt if somebody
   * else calls it.
   *
   * @return a global sequence ID that nobody else has seen.
   */
  uint32_t get_global_seq(uint32_t old=0) {
    global_seq_lock.lock();
    if (old > global_seq)
      global_seq = old;
    uint32_t ret = ++global_seq;
    global_seq_lock.unlock();
    return ret;
  }
  /**
   * Get the protocol version we support for the given peer type: either
   * a peer protocol (if it matches our own), the protocol version for the
   * peer (if we're connecting), or our protocol version (if we're accepting).
   */
  int get_proto_version(int peer_type, bool connect);

  /**
   * Fill in the address and peer type for the local connection, which
   * is used for delivering messages back to ourself.
   */
  void init_local_connection() {
    {
      Mutex::Locker l(lock);
      local_connection->set_peer_addr(my_inst.addr);
      local_connection->set_peer_type(my_inst.name.type());
      static_cast<LocalConnection *>(local_connection.ptr())->shutdown = false;
    }
    ms_deliver_handle_fast_connect(local_connection.ptr());
  }

  /**
   * Unregister connection from `conns`
   */
  void unregister_conn(AsyncConnectionRef conn) {
    Mutex::Locker l(lock);
    _unregister_conn(conn);
  }
  /**
   * @} // AsyncMessenger Internals
   */
} ;

} //namespace msgr

#endif /* MSGR_ASYNCMESSENGER_H */
