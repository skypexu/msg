#include "msgr/Message.h"
#include "msgr/Clock.h"

#include "AsyncMessenger.h"
#include "LocalConnection.h"

#include <cxxabi.h>
#include <execinfo.h>

#define dout_subsys msgr_subsys_ms

namespace msgr {

LocalConnection::LocalConnection(MsgrContext *cct, AsyncMessenger *msgr)
: Connection(cct, msgr)
{
  inc_total_local_alloc();
  cct_ = cct;
  msgr_ = msgr;
  shutdown = false;
  conn_id = msgr_->dispatch_queue.get_id();
}

LocalConnection::~LocalConnection()
{
  dec_total_local_alloc();
}

bool LocalConnection::is_connected()
{
  return false;
}

int LocalConnection::send_message(Message *m)
{
  assert(msgr_);
  Mutex::Locker locker(lock);

  if (shutdown) {
    ldout(msgr_->cct, 10) << __func__ << " local connection closed."
        << " Drop message " << m << dendl;
    m->put();
    return 0;
  }

  // set envelope
  m->get_header().src = msgr_->get_myname();
  if (!m->get_priority())
    m->set_priority(msgr_->get_default_send_priority());
  msgr_->dispatch_queue.local_delivery(m, m->get_priority());
  return 0;
}

void LocalConnection::send_keepalive()
{
  ldout(cct_, 10) << "send_keepalive local" << dendl;
}

void LocalConnection::mark_down()
{
  ldout(cct_, 10) << "mark_down " << " local done" << dendl;
}

void LocalConnection::stop()
{
  Mutex::Locker locker(lock);
  if (!shutdown) {
    shutdown = true;
    msgr_->dispatch_queue.discard_queue(conn_id);
    msgr_->dispatch_queue.queue_reset(this);
  }
}

void LocalConnection::mark_disposable()
{
  ldout(cct_, 10) << "mark_disposable " << " local done" << dendl;
}

} // namespace msgr
