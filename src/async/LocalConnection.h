#ifndef LOCALCONNECTION_2e0628c6cc92_H
#define LOCALCONNECTION_2e0628c6cc92_H

#include "msgr/Connection.h"

namespace msgr {

class MsgrContext;
class AsyncMessenger;
class Message;

class LocalConnection : public Connection
{
    MsgrContext *cct_;
    AsyncMessenger *msgr_;
    uint64_t conn_id;
    bool shutdown;

public:
    LocalConnection(MsgrContext *cct, AsyncMessenger *msgr);
    virtual ~LocalConnection();

    virtual bool is_connected();
    virtual int send_message(Message *m);
    virtual void send_keepalive();
    virtual void mark_down();
    virtual void mark_disposable();

private:
    // used by AsyncMesenger::mark_down_all
    void stop();

    friend class AsyncMessenger;
};

} // namespace msgr

#endif // LOCALCONNECTION_2e0628c6cc92_H
