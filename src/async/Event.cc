#include "Event.h"
#include <iostream>

namespace msgr {

void EventCenter::l_release(EV_P)
{
    EventCenter *center = static_cast<EventCenter *>(ev_userdata(EV_A));
    center->lock.Unlock();
}

void EventCenter::l_acquire(EV_P)
{
    EventCenter *center = static_cast<EventCenter *>(ev_userdata(EV_A));
    center->lock.Lock();
}

void EventCenter::invoke_pending_cb(EV_P)
{
    EventCenter *center = static_cast<EventCenter *>(ev_userdata(EV_A));

    ev_invoke_pending(EV_A);

    center->lock.Unlock();
    center->external_lock.Lock();
    while (!center->external_events.empty()) {
      EventCallbackRef e = center->external_events.front();
      center->external_events.pop_front();
      center->external_lock.Unlock();
      if (e)
        e->do_request(0);
      center->external_lock.Lock();
    }
    center->external_lock.Unlock();
    center->lock.Lock();
}

void EventCenter::on_wakeup()
{
}

void EventCenter::dispatch_event_external(EventCallbackRef e)
{
    external_lock.Lock();
    external_events.push_back(e);
    external_lock.Unlock();
    wakeup();
}

void EventCenter::maybe_wakeup()
{
    if (pthread_self() != owner)
        wakeup();
}

}
