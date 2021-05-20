#include "msgr/atomic.h"
#include "msgr/environment.h"
#include "msgr/Connection.h"

using namespace std;

namespace {
    msgr::atomic_count<unsigned> conn_total_alloc;
    msgr::atomic_count<unsigned> conn_total_local_alloc;
    bool conn_track_alloc = get_env_bool("MSGR_CONNECTION_TRACK");
    pthread_mutex_t all_lock = PTHREAD_MUTEX_INITIALIZER;
    std::set<msgr::Connection *> all_conns;
}

namespace msgr {
Connection::Connection(MsgrContext *pct, Messenger *m)
// we are managed exlusively by ConnectionRef; make it so you can
//   ConnectionRef foo = new Connection;
: RefCountedObject(pct, 0),
  lock("Connection::lock"), msgr(m), priv(NULL),
  peer_type(-1), features(0), rx_buffers_version(0),
  failed(false)
{

    pthread_mutex_lock(&all_lock);
    inc_total_alloc();
    all_conns.insert(this);
    pthread_mutex_unlock(&all_lock);
}                                                                             
                                                                               
Connection::~Connection()
{                                                       
    pthread_mutex_lock(&all_lock);
    dec_total_alloc();                                                          
    all_conns.erase(this);
    pthread_mutex_unlock(&all_lock);

    //generic_dout(0) << "~Connection " << this << dendl;
    if (priv) {
      //generic_dout(0) << "~Connection " << this << " dropping priv " << priv << dendl;
      priv->put();
    }
}

void Connection::inc_total_alloc()
{
    if (conn_track_alloc)
        conn_total_alloc.inc();
}

void Connection::dec_total_alloc()
{
    if (conn_track_alloc)
      conn_total_alloc.dec();
}

unsigned Connection::get_total_alloc()
{
    return conn_total_alloc.read();
}

void Connection::inc_total_local_alloc()
{
    if (conn_track_alloc)
        conn_total_local_alloc.inc();
}

void Connection::dec_total_local_alloc()
{
    if (conn_track_alloc)
      conn_total_local_alloc.dec();
}

unsigned Connection::get_total_local_alloc()
{
    return conn_total_local_alloc.read();
}

void Connection::dump(std::ostream &os)
{
    os << "connection dump: " << this <<  " " << get_nref() << '\n'; 
}

void Connection::dump_all(std::ostream &os)
{
    pthread_mutex_lock(&all_lock);
    for (set<Connection *>::iterator it = all_conns.begin(); it != all_conns.end();
        ++it) {
        (*it)->dump(os);
    }
    pthread_mutex_unlock(&all_lock);
}

void Connection::for_each(bool (*cb)(Connection *, void *), void *arg)
{
    pthread_mutex_lock(&all_lock);
    for (set<Connection *>::iterator it = all_conns.begin(); it != all_conns.end();
        ++it) {
        if (cb(*it, arg))
            break;
    }
    pthread_mutex_unlock(&all_lock);
}

} // namespace msgr
