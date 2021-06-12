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

#include "acconfig.h"

#include <errno.h>
#include <iostream>
#include <fstream>
#include <sched.h>
#include <unistd.h>

#include "msgr/str_list.h"
#include "msgr/strtol.h"
#include "msgr/MsgrConfig.h"
#include "msgr/cpp_errno.h"
#include "msgr/Crypto.h"
#include "../FIFOQueue.h"

#include "AsyncMessenger.h"

using namespace std;

namespace {

using namespace msgr;

class FIFOWrapper : public DispatchQueue::Queue
{
    typedef DispatchQueue::QueueItem QueueItem;
    FIFOQueue<QueueItem, uint64_t> q;
public:
    virtual bool empty() const { return q.empty(); }
    virtual unsigned length() const { return q.length(); }
    virtual void enqueue_strict(uint64_t k, unsigned priority, QueueItem item)
    {
        q.enqueue_strict(k, priority, item);
    }
    virtual void enqueue(uint64_t k, unsigned priority, unsigned cost, QueueItem item)
    {
        q.enqueue(k, priority, cost, item);
    }
    virtual void remove_by_class(uint64_t k, std::list<QueueItem> *removed)
    {
        q.remove_by_class(k, removed);
    }
    virtual QueueItem dequeue()
    {
        return q.dequeue();
    }
};

}

namespace msgr {

#define dout_subsys msgr_subsys_ms
#undef dout_prefix
#define dout_prefix _prefix(dout_, this)
static ostream& _prefix(std::ostream *_dout, AsyncMessenger *m) {
  return *_dout << "-- " << m->get_myaddr() << " ";
}

static ostream& _prefix(std::ostream *_dout, Processor *p) {
  return *_dout << " Processor -- ";
}

static ostream& _prefix(std::ostream *_dout, Worker *w) {
  return *_dout << " Worker -- ";
}

static ostream& _prefix(std::ostream *_dout, WorkerPool *p) {
  return *_dout << " WorkerPool -- ";
}

/*******************
 * Processor
 */

int Processor::bind(const entity_addr_t &bind_addr, const set<int>& avoid_ports)
{
  const md_config_t *conf = msgr->cct->conf_;
  // bind to a socket
  ldout(msgr->cct, 10) << __func__ << dendl;

  int err, family;
  switch (bind_addr.get_family()) {
    case AF_INET:
    case AF_INET6:
      family = bind_addr.get_family();
      break;

    default:
      // bind_addr is empty
      family = conf->ms_bind_ipv6 ? AF_INET6 : AF_INET;
  }

  /* socket creation */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

  listen_sd = ::socket(family, SOCK_STREAM|SOCK_CLOEXEC, 0);
  if (listen_sd < 0) {
    err = errno;
    lderr(msgr->cct) << __func__ << " unable to create socket: "
        << cpp_strerror(err) << dendl;
    return -err;
  }

  int r = net.set_nonblock(listen_sd);
  if (r < 0) {
    err = errno;
    ::close(listen_sd);
    listen_sd = -1;
    return -err;
  }
  // use whatever user specified (if anything)
  entity_addr_t listen_addr = bind_addr;
  listen_addr.set_family(family);

  /* bind to port */
  int rc = -1;
  r = -1;

  for (int i = 0; i < conf->ms_bind_retry_count; i++) {
    if (i > 0) {
      lderr(msgr->cct) << __func__ << " was unable to bind. Trying again in "
                       << conf->ms_bind_retry_delay << " seconds " << dendl;
      sleep(conf->ms_bind_retry_delay);
    }

    if (listen_addr.get_port()) {
      // specific port
      // reuse addr+port when possible
      int on = 1;
      rc = ::setsockopt(listen_sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
      if (rc < 0) {
        err = errno;
        lderr(msgr->cct) << __func__ << " unable to setsockopt: " << cpp_strerror(err) << dendl;
        r = -err;
        continue;
      }

      rc = ::bind(listen_sd, (struct sockaddr *) &listen_addr.ss_addr(), listen_addr.addr_size());
      if (rc < 0) {
        err = errno;
        lderr(msgr->cct) << __func__ << " unable to bind to " << listen_addr.ss_addr()
                         << ": " << cpp_strerror(err) << dendl;
        r = -err;
        continue;
      }
    } else {
      // try a range of ports
      for (int port = msgr->cct->conf_->ms_bind_port_min; port <= msgr->cct->conf_->ms_bind_port_max; port++) {
        if (avoid_ports.count(port))
          continue;

        listen_addr.set_port(port);
        rc = ::bind(listen_sd, (struct sockaddr *) &listen_addr.ss_addr(), listen_addr.addr_size());
        if (rc == 0)
          break;
      }
      if (rc < 0) {
        err = errno;
        lderr(msgr->cct) << __func__ << " unable to bind to " << listen_addr.ss_addr()
                         << " on any port in range " << msgr->cct->conf_->ms_bind_port_min
                         << "-" << msgr->cct->conf_->ms_bind_port_max << ": "
                         << cpp_strerror(err) << dendl;
        listen_addr.set_port(0);
        r = -err;
        continue;
      }
      ldout(msgr->cct, 10) << __func__ << " bound on random port " << listen_addr << dendl;
    }
    if (rc == 0)
      break;
  }
  // It seems that binding completely failed, return with that exit status
  if (rc < 0) {
    err = errno;
    lderr(msgr->cct) << __func__ << " was unable to bind after " << conf->ms_bind_retry_count
                     << " attempts: " << cpp_strerror(err) << dendl;
    ::close(listen_sd);
    listen_sd = -1;
    return r;
  }

  // what port did we get?
  socklen_t llen = sizeof(listen_addr.ss_addr());
  rc = ::getsockname(listen_sd, (sockaddr*)&listen_addr.ss_addr(), &llen);
  if (rc < 0) {
    rc = -errno;
    lderr(msgr->cct) << __func__ << " failed getsockname: " << cpp_strerror(rc) << dendl;
    ::close(listen_sd);
    listen_sd = -1;
    return rc;
  }

  ldout(msgr->cct, 10) << __func__ << " bound to " << listen_addr << dendl;

  // listen!
  rc = ::listen(listen_sd, 128);
  if (rc < 0) {
    rc = -errno;
    lderr(msgr->cct) << __func__ << " unable to listen on " << listen_addr
        << ": " << cpp_strerror(rc) << dendl;
    ::close(listen_sd);
    listen_sd = -1;
    return rc;
  }

  msgr->set_myaddr(bind_addr);
  if (bind_addr != entity_addr_t())
    msgr->learned_addr(bind_addr);
  else
    assert(msgr->get_need_addr());  // should still be true.

  if (msgr->get_myaddr().get_port() == 0) {
    msgr->set_myaddr(listen_addr);
  }
  entity_addr_t addr = msgr->get_myaddr();
  addr.nonce = nonce;
  msgr->set_myaddr(addr);

  msgr->init_local_connection();

  ldout(msgr->cct,1) << __func__ << " bind my_inst.addr is " << msgr->get_myaddr() << dendl;
  return 0;
}

int Processor::rebind(const set<int>& avoid_ports)
{
  ldout(msgr->cct, 1) << __func__ << " rebind avoid " << avoid_ports << dendl;

  entity_addr_t addr = msgr->get_myaddr();
  set<int> new_avoid = avoid_ports;
  new_avoid.insert(addr.get_port());
  addr.set_port(0);

  // adjust the nonce; we want our entity_addr_t to be truly unique.
  nonce += 1000000;
  msgr->my_inst.addr.nonce = nonce;
  ldout(msgr->cct, 10) << __func__ << " new nonce " << nonce << " and inst " << msgr->my_inst << dendl;

  ldout(msgr->cct, 10) << __func__ << " will try " << addr << " and avoid ports " << new_avoid << dendl;
  return bind(addr, new_avoid);
}

int Processor::start(Worker *w)
{
  ldout(msgr->cct, 1) << __func__ << " " << dendl;

  Mutex::Locker locker(lock);
  if (listen_sd > 0 && worker == NULL) {
    Mutex::Locker locker(w->center.lock);
    worker = w;
    ev_read.set(w->center);
    ev_read.set(listen_sd, ev::READ);
    ev_read.set<Processor, &Processor::accept>(this);
    ev_read.start();
    w->center.wakeup();
  }

  return 0;
}

void Processor::accept()
{
  Mutex::Unlocker unlocker(worker->center.lock);

  ldout(msgr->cct, 10) << __func__ << " listen_sd=" << listen_sd << dendl;
  int errors = 0;
  while (errors < 4) {
    entity_addr_t addr;
    socklen_t slen = sizeof(addr.ss_addr());
    int sd;
#if defined(MSGR_HAVE_ACCEPT4) && defined(SOCK_CLOEXEC)
    sd = ::accept4(listen_sd, (sockaddr*)&addr.ss_addr(), &slen, SOCK_CLOEXEC);
#else
    sd = ::accept(listen_sd, (sockaddr*)&addr.ss_addr(), &slen);
#endif
    if (sd >= 0) {
      errors = 0;
      ldout(msgr->cct, 10) << __func__ << " accepted incoming on sd " << sd << dendl;

      msgr->add_accept(sd);
      continue;
    } else {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN) {
        break;
      } else {
        int err = errno;
        errors++;
        ldout(msgr->cct, 20) << __func__ << " no incoming connection?  sd = " << sd
                             << " errno " << errno << " " << cpp_strerror(err) << dendl;
      }
    }
  }
}

void Processor::do_stop()
{
  ldout(msgr->cct,10) << __func__ << dendl;

  Mutex::Locker locker1(lock);
  Mutex::Locker locker2(worker->center.lock);
  ev_read.stop();
  stopped = true;
  cond.Signal();
}

void Processor::stop()
{
  ldout(msgr->cct,10) << __func__ << dendl;

  Mutex::Locker locker(lock);
  if (listen_sd >= 0) {
    if (worker) {
        stopped = false;
        worker->center.dispatch_event_external(EventCallbackRef(new C_ProcessorStop(this)));
        while (!stopped)
            cond.Wait(lock);
        worker = NULL;
    }
    ::shutdown(listen_sd, SHUT_RDWR);
    ::close(listen_sd);
    listen_sd = -1;
  }
}

////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////
void Worker::stop()
{
  ldout(cct, 10) << __func__ << dendl;
  Mutex::Locker locker(center.lock);
  done = true;
  center.wakeup();
}

void *Worker::entry()
{
  ldout(cct, 10) << __func__ << " starting" << dendl;

  Thread::set_name("worker");

  if (cct->conf_->ms_async_set_affinity) {
    int cpuid;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    cpuid = pool->get_cpuid(id);
    if (cpuid < 0) {
      cpuid = sched_getcpu();
    }

    if (cpuid < CPU_SETSIZE) {
      CPU_SET(cpuid, &cpuset);

      if (sched_setaffinity(0, sizeof(cpuset), &cpuset) < 0) {
        ldout(cct, 0) << __func__ << " sched_setaffinity failed: "
            << cpp_strerror(errno) << dendl;
      }
      /* guaranteed to take effect immediately */
      sched_yield();
    }
  }

  Mutex::Locker locker(center.lock);
  center.set_owner(pthread_self());
  while (!done)
    center.loop(ev::ONCE);

  return 0;
}

/*******************
 * WorkerPool
 *******************/
const string WorkerPool::name = "AsyncMessenger::WorkerPool";

WorkerPool::WorkerPool(MsgrContext *c): cct(c), seq(0), started(false),
                                        barrier_lock("WorkerPool::WorkerPool::barrier_lock"),
                                        barrier_count(0)
{
  assert(cct->conf_->ms_async_op_threads > 0);
  for (int i = 0; i < cct->conf_->ms_async_op_threads; ++i) {
    Worker *w = new Worker(cct, this, i);
    workers.push_back(w);
  }
  vector<string> corestrs;
  get_str_vec(cct->conf_->ms_async_affinity_cores, corestrs);
  for (vector<string>::iterator it = corestrs.begin();
       it != corestrs.end(); ++it) {
    string err;
    int coreid = strict_strtol(it->c_str(), 10, &err);
    if (err == "")
      coreids.push_back(coreid);
    else
      lderr(cct) << __func__ << " failed to parse " << *it << " in " << cct->conf_->ms_async_affinity_cores << dendl;
  }
}

WorkerPool::~WorkerPool()
{
  for (uint64_t i = 0; i < workers.size(); ++i) {
    workers[i]->stop();
    workers[i]->join();
    delete workers[i];
  }
}

void WorkerPool::start()
{
  Mutex::Locker locker(barrier_lock);
  if (!started) {
    for (uint64_t i = 0; i < workers.size(); ++i) {
      workers[i]->create(cct->conf_->ms_async_thread_stack_bytes);
    }
    started = true;
  }
}

void WorkerPool::stop()
{
  Mutex::Locker locker(barrier_lock);
  if (started) {
    for (uint64_t i = 0; i < workers.size(); ++i) {
      workers[i]->stop();
      workers[i]->join();
    }
    started = false;
  }
}

void WorkerPool::barrier()
{
  ldout(cct, 10) << __func__ << " started." << dendl;
  pthread_t cur = pthread_self();
  for (vector<Worker*>::iterator it = workers.begin(); it != workers.end(); ++it) {
    cur = cur;
    assert(cur != (*it)->center.get_owner());
    (*it)->center.dispatch_event_external(EventCallbackRef(new C_barrier(this)));
    barrier_count.inc();
  }
  ldout(cct, 10) << __func__ << " wait for " << barrier_count.read() << " barrier" << dendl;
  Mutex::Locker l(barrier_lock);
  while (barrier_count.read())
    barrier_cond.Wait(barrier_lock);

  ldout(cct, 10) << __func__ << " end." << dendl;
}


/*******************
 * AsyncMessenger
 */

AsyncMessenger::AsyncMessenger(MsgrContext *cct, entity_name_t name,
                              string mname, uint64_t _nonce)
  : SimplePolicyMessenger(cct, name,mname, _nonce),
    lock("AsyncMessenger::lock"), pool(0),
    nonce(_nonce), need_addr(true), did_bind(false),
    global_seq(0),
    cluster_protocol(0),
    processor(this, cct, _nonce),
    dispatch_queue(cct, this, new FIFOWrapper()),
    start_lock("AsyncMessenger::start_lock"),
    shutdown_called(false)
{
  cct->lookup_or_create_singleton_object<WorkerPool>(pool, WorkerPool::name);
  pool->start();
  local_connection = new LocalConnection(cct, this);
  init_local_connection();
}

/**
 * Destroy the AsyncMessenger. Pretty simple since all the work is done
 * elsewhere.
 */
AsyncMessenger::~AsyncMessenger()
{
  assert(!did_bind); // either we didn't bind or we shut down the Processor
}

void AsyncMessenger::ready()
{
  ldout(cct,10) << __func__ << " " << get_myaddr() << dendl;
  dispatch_queue.start();

  lock.Lock();
  if (did_bind) {
      Worker *w = pool->get_worker();
      processor.start(w);
  }
  lock.Unlock();
}

int AsyncMessenger::shutdown()
{
  ldout(cct,10) << __func__ << " " << get_myaddr() << dendl;

  Mutex::Locker locker(start_lock);
  if (!started)
    return -1;
  if (shutdown_called)
    return 0;
  shutdown_called = true;
  processor.stop();
  did_bind = false;
  mark_down_all();
  pool->barrier();
  static_cast<LocalConnection *>(local_connection.ptr())->stop();
  dispatch_queue.shutdown();
  return 0;
}

int AsyncMessenger::bind(const entity_addr_t &bind_addr)
{
  Mutex::Locker locker(start_lock);
  if (started) {
    ldout(cct,10) << __func__ << " already started" << dendl;
    return -1;
  }
  ldout(cct, 10) << __func__ << " bind " << bind_addr << dendl;

  // bind to a socket
  set<int> avoid_ports;
  int r = processor.bind(bind_addr, avoid_ports);
  if (r >= 0)
    did_bind = true;
  return r;
}

int AsyncMessenger::rebind(const set<int>& avoid_ports)
{
  Mutex::Locker locker(start_lock);

  ldout(cct,1) << __func__ << " rebind avoid " << avoid_ports << dendl;
  assert(did_bind);

  processor.stop();
  mark_down_all();
  int r = processor.rebind(avoid_ports);
  if (r == 0) {
    Worker *w = pool->get_worker();
    processor.start(w);
  }
  return r;
}

void AsyncMessenger::unbind()
{
  Mutex::Locker locker(start_lock);
  if (did_bind)
    processor.stop();
  did_bind = false;
}

int AsyncMessenger::start()
{
  Mutex::Locker locker(start_lock);
  if (started) {
    lderr(cct) << __func__ << " start, already started" << dendl;
    return -1; 
  }

  ldout(cct,1) << __func__ << " start" << dendl;

  // register at least one entity, first!
  assert(my_inst.name.type() >= 0);

  started = true;

  if (!did_bind) {
    my_inst.addr.nonce = nonce;
    init_local_connection();
  } else {
  //  Worker *w = pool->get_worker();
  //  processor.start(w);
  }
//  dispatch_queue.start();
  return 0;
}

void AsyncMessenger::wait()
{
  Mutex::Locker locker(start_lock);
  if (!started)
    return;

  assert(!did_bind); // shutdown should be called

  if (dispatch_queue.is_started()) {
    ldout(cct,10) << "wait: waiting for dispatch queue" << dendl;
    Mutex::Unlocker unlocker(start_lock);
    dispatch_queue.wait();
    ldout(cct,10) << "wait: dispatch queue is stopped" << dendl;
  }

  shutdown_called = false;
  started = false;   

  ldout(cct, 10) << __func__ << ": done." << dendl;                             
  ldout(cct, 1) << __func__ << " complete." << dendl;                           
}

AsyncConnectionRef AsyncMessenger::add_accept(int sd)
{
  Worker *w;
  {
  Mutex::Locker locker(lock);
  w = pool->get_worker();
  }
  AsyncConnectionRef conn;
  conn = new AsyncConnection(cct, this, &w->center);

  Mutex::Locker locker(lock);
  ldout(cct, 1) << __func__ << " locked?" << w->center.lock.is_locked_by_me() << dendl;
  accepting_conns.insert(conn);
  conn->accept(sd);
  return conn;
}

AsyncConnectionRef AsyncMessenger::create_connect(const entity_addr_t& addr, int type)
{
  assert(lock.is_locked());
  assert(addr != my_inst.addr);

  ldout(cct, 10) << __func__ << " " << addr
      << ", creating connection and registering" << dendl;

  // create connection
  Worker *w = pool->get_worker();
  AsyncConnectionRef conn = new AsyncConnection(cct, this, &w->center);
  conn->connect(addr, type);
  assert(!conns.count(addr));
  conns[addr] = conn;

  return conn;
}

ConnectionRef AsyncMessenger::get_connection(const entity_inst_t& dest)
{
  Mutex::Locker l(lock);
  if (my_inst.addr == dest.addr) {
    // local
    return local_connection;
  }

  AsyncConnectionRef conn = _lookup_conn(dest.addr);
  if (conn) {
    ldout(cct, 10) << __func__ << " " << dest << " existing " << conn << dendl;
  } else {
    conn = create_connect(dest.addr, dest.name.type());
    ldout(cct, 10) << __func__ << " " << dest << " new " << conn << dendl;
  }

  return conn;
}

ConnectionRef AsyncMessenger::get_loopback_connection()
{
  return local_connection;
}

/**
 * If my_inst.addr doesn't have an IP set, this function
 * will fill it in from the passed addr. Otherwise it does nothing and returns.
 */
void AsyncMessenger::set_addr_unknowns(entity_addr_t &addr)
{
  lock.Lock();
  if (my_inst.addr.is_blank_ip()) {
    int port = my_inst.addr.get_port();
    my_inst.addr.addr = addr.addr;
    my_inst.addr.set_port(port);
    lock.Unlock();
    init_local_connection();
  } else {
    lock.Unlock();
  }
}

void AsyncMessenger::mark_down_all()
{
  ldout(cct,1) << __func__ << " " << dendl;

  Mutex::Locker locker(lock);
  for (set<AsyncConnectionRef>::iterator it = accepting_conns.begin();
       it != accepting_conns.end(); ++it) {
    AsyncConnectionRef p = *it;
    ldout(cct, 5) << __func__ << " mark down accepting_conn " << p.ptr() << dendl;
    p->stop();
  }

  for (unordered_map<entity_addr_t, AsyncConnectionRef>::iterator it = conns.begin();
       it != conns.end(); ++it) {
    AsyncConnectionRef p = it->second;
    ldout(cct, 5) << __func__ << " mark down " << it->first << " " << p << dendl;
    p->stop();
  }
}

int AsyncMessenger::get_proto_version(int peer_type, bool connect)
{
  int my_type = my_inst.name.type();

  // set reply protocol version
  if (peer_type == my_type) {
    // internal
    return cluster_protocol;
  } else {
    // public
    if (connect) {
      switch (peer_type) {
        case MSGR_ENTITY_TYPE_OSD: return MSGR_OSDC_PROTOCOL;
        case MSGR_ENTITY_TYPE_MDS: return MSGR_MDSC_PROTOCOL;
        case MSGR_ENTITY_TYPE_MON: return MSGR_MONC_PROTOCOL;
      }
    } else {
      switch (my_type) {
        case MSGR_ENTITY_TYPE_OSD: return MSGR_OSDC_PROTOCOL;
        case MSGR_ENTITY_TYPE_MDS: return MSGR_MDSC_PROTOCOL;
        case MSGR_ENTITY_TYPE_MON: return MSGR_MONC_PROTOCOL;
      }
    }
  }
  return 0;
}

void AsyncMessenger::learned_addr(const entity_addr_t &peer_addr_for_me)
{
  // be careful here: multiple threads may block here, and readers of
  // my_inst.addr do NOT hold any lock.

  // this always goes from true -> false under the protection of the
  // mutex.  if it is already false, we need not retake the mutex at
  // all.
  if (!need_addr)
    return ;
  lock.Lock();
  if (need_addr) {
    need_addr = false;
    entity_addr_t t = peer_addr_for_me;
    t.set_port(my_inst.addr.get_port());
    my_inst.addr.addr = t.addr;
    ldout(cct, 1) << __func__ << " learned my addr " << my_inst.addr << dendl;
    lock.Unlock();
    init_local_connection();
  } else {
    lock.Unlock();
  }
}

} // namespace msgr
