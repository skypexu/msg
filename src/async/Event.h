#ifndef MSGR_EVENT_H
#define MSGR_EVENT_H

#include <list>
#include <memory>
#include <ev++.h>
#include <pthread.h>

#include "msgr/Mutex.h"

namespace msgr {

class MsgrContext;

class EventCallback {
 public:
  virtual void do_request(int fd_or_id) = 0;
  virtual ~EventCallback() {}       // we want a virtual destructor!!!
};

typedef std::shared_ptr<EventCallback> EventCallbackRef;

struct EventCenter : public ev::dynamic_loop {
    MsgrContext *cct;
    Mutex lock;
    Mutex external_lock;
    pthread_t owner;
    ev::async async;
    std::list<EventCallbackRef> external_events;

    EventCenter(MsgrContext *_cct, unsigned int flags = ev::AUTO)
     :dynamic_loop(flags), cct(_cct), lock("EventCenter lock"),
      external_lock("EventCenter extern_lock")
    {
        ev_set_userdata(*this, this);
        ev_set_loop_release_cb(*this, l_release, l_acquire);
        ev_set_invoke_pending_cb(*this, invoke_pending_cb);
        async.set(*this);
        async.set<EventCenter, &EventCenter::on_wakeup>(this);
        async.start();
    }
    ~EventCenter() { async.stop(); }
    void set_owner(pthread_t p) { owner = p; }
    pthread_t get_owner() { return owner; }
    void maybe_wakeup();
    void wakeup() { async.send(); }
    // Used by external thread
    void dispatch_event_external(EventCallbackRef e);

private:
    void on_wakeup();

    static void l_release(EV_P);
    static void l_acquire(EV_P);
    static void invoke_pending_cb(EV_P);
};

} //namespace msgr
#endif
