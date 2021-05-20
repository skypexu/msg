// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef MSGR_THROTTLE_H
#define MSGR_THROTTLE_H

#include <list>
#include <string.h>
#include <stdint.h>

#include "Mutex.h"
#include "Cond.h"
#include "atomic.h"

namespace msgr {

class MsgrContext;
//class PerfCounters;

class Throttle {
public:
  typedef void (*Callback)(Throttle *thrtl, void *arg, bool abort);

private:
  MsgrContext *cct;
  std::string name;
//  PerfCounters *logger;
  atomic_t count, max;
  Mutex lock;
  struct Waiter {
    Cond *cv;
    Callback cb;
    void *arg;
    int64_t c;

    Waiter(Cond *_cv) : cv(_cv), cb(nullptr), arg(nullptr), c(0) {}
    Waiter(int64_t _c, Callback _cb, void *_arg) : cv(nullptr), cb(_cb), arg(_arg), c(_c) {}
  };
  std::list<Waiter* > wait_queue;
  bool use_perf;
  
public:
  Throttle(MsgrContext *cct, std::string n, int64_t m = 0, bool _use_perf = true);
  ~Throttle();


private:
  void _reset_max(int64_t m);
  bool _should_wait(int64_t c) {
    int64_t m = max.read();
    int64_t cur = count.read();
    return
      m &&
      ((c <= m && cur + c > m) || // normally stay under max
       (c >= m && cur > m));     // except for large c
  }

  bool _wait(int64_t c);
  bool _wait(int64_t c, Callback cb, void *arg);
  void signal_first();

public:
  int64_t get_current() {
    return count.read();
  }

  int64_t get_max() { return max.read(); }

  bool wait(int64_t m = 0);

  int64_t take(int64_t c = 1);
  bool get(int64_t c = 1, int64_t m = 0);
  bool get(Callback cb, void *arg, int64_t c, int64_t m = 0);

  /**
   * Returns true if it successfully got the requested amount,
   * or false if it would block.
   */
  bool get_or_fail(int64_t c = 1);
  int64_t put(int64_t c = 1);
};

/**
 * @class SimpleThrottle
 * This is a simple way to bound the number of concurrent operations.
 *
 * It tracks the first error encountered, and makes it available
 * when all requests are complete. wait_for_ret() should be called
 * before the instance is destroyed.
 *
 * Re-using the same instance isn't safe if you want to check each set
 * of operations for errors, since the return value is not reset.
 */
class SimpleThrottle {
public:
  SimpleThrottle(uint64_t max, bool ignore_enoent);
  ~SimpleThrottle();
  void start_op();
  void end_op(int r);
  bool pending_error() const;
  int wait_for_ret();
private:
  mutable Mutex m_lock;
  Cond m_cond;
  uint64_t m_max;
  uint64_t m_current;
  int m_ret;
  bool m_ignore_enoent;
};

#if 0
class C_SimpleThrottle : public Context {
public:
  C_SimpleThrottle(SimpleThrottle *throttle) : m_throttle(throttle) {
    m_throttle->start_op();
  }
  virtual void finish(int r) {
    m_throttle->end_op(r);
  }
private:
  SimpleThrottle *m_throttle;
};
#endif

} // namespace msgr
#endif
