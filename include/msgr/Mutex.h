#ifndef MSGR_MUTEX_8f3158a48733_H
#define MSGR_MUTEX_8f3158a48733_H

#include "msgr_assert.h"
#include "MsgrContext.h"
#include "Thread.h"

#include <pthread.h>
#include <string>

namespace msgr {

class Mutex {
private:
  std::string name;
  int id;
  bool recursive;
  bool lockdep;

  pthread_mutex_t _m;
  int nlock;
  pthread_t locked_by;
  const char *locked_by_name;

  // don't allow copying.
  void operator=(const Mutex &M);
  Mutex(const Mutex &M);

public:
  Mutex(const std::string &n, bool r = false);
  ~Mutex();
  bool is_locked() const {
    return (nlock > 0);
  }
  bool is_locked_by_me() const {
    return nlock > 0 && locked_by == pthread_self();
  }

  bool TryLock() {
    int r = pthread_mutex_trylock(&_m);
    if (r == 0) {
      _post_lock();
    }
    return r == 0;
  }

  void Lock();

  void _post_lock() {
    if (!recursive) {
      assert(nlock == 0);
      locked_by = pthread_self();
#ifndef NDEBUG
      locked_by_name = Thread::get_name();
#endif
    };
    nlock++;
  }

  void _pre_unlock() {
    assert(nlock > 0);
    --nlock;
    if (!recursive) {
      assert(locked_by == pthread_self());
      locked_by = 0;
      locked_by_name = 0;
      assert(nlock == 0);
    }
  }
  void Unlock();

  friend class Cond;


public:
  class Locker {
    Mutex &mutex;

  public:
    Locker(Mutex& m) : mutex(m) {
      mutex.Lock();
    }
    ~Locker() {
      mutex.Unlock();
    }
  };

  class Unlocker {
    Mutex &mutex;

  public:
    Unlocker(Mutex &m) : mutex(m) {
      mutex.Unlock();
    }
    ~Unlocker() {
      mutex.Lock();
    }
  };
};

} // namespace msgr
#endif
