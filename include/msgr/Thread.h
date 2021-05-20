#ifndef MSGR_THREAD_2bd404c1a80744a8b19614f2aed89e93_H
#define MSGR_THREAD_2bd404c1a80744a8b19614f2aed89e93_H

#include <pthread.h>
#include <sys/types.h>

namespace msgr {

class Thread {
 private:
  pthread_t thread_id;
  pid_t pid;
  int ioprio_class, ioprio_priority;

  void *entry_wrapper();

 public:
  Thread(const Thread& other);
  const Thread& operator=(const Thread& other);

  Thread();
  virtual ~Thread();

  static void set_name(const char *name);
  static const char *get_name();

 protected:
  virtual void *entry() = 0;

 private:
  static void *_entry_func(void *arg);

 public:
  const pthread_t &get_thread_id();
  pid_t get_pid() const { return pid; }
  bool is_started() const;
  bool am_self();
  int kill(int signal);
  int try_create(size_t stacksize);
  void create(size_t stacksize = 0);
  int join(void **prval = 0);
  int detach();
  int set_ioprio(int cls, int prio);
};

} // namespace msgr
#endif
