#include <iostream>
#include <unistd.h>

#include "msgr/Messenger.h"
#include "msgr/dout.h"
#include "sample_msg.h"

#define dout_subsys msgr_subsys_ms


using namespace std;
using namespace msgr;
MsgrContext cct, *g_context = &cct;

#include "fake_dispatcher.h"

class SrvDispatcher: public Dispatcher
{
public:
    SrvDispatcher(MsgrContext *cct)
        :Dispatcher(cct)
    {
    }

    bool ms_can_fast_dispatch_any() const { return true; }

    virtual bool ms_can_fast_dispatch(Message *m) const override
    {
        cout << __func__ << "\n";
        switch(m->get_type()) {
        case SAMPLE_MSG_1:
            return true;
        }
        return false; 
    }

    virtual bool ms_dispatch(Message *m) override {
        cout << __func__ << "msg type:" << m->get_type() << '\n';
        return false;
    }

    virtual void ms_fast_dispatch(Message *m) override {
        cout << __func__ << "\n";
        switch(m->get_type()) {
        case SAMPLE_MSG_1: {
            bufferlist::iterator it;
            for (it = m->get_data().begin(); !it.end(); ++it)
               cout << *it;
            m->get_connection()->send_message(m);
            cout << __func__ << "got samplemsg data length" << m->get_data().length() << '\n';
            }
            return;
        }
    }

    virtual void ms_handle_accept(Connection *con) override
    {
        cout << __func__ << " nref:" << con->get_nref() << '\n';
        return;
    }

    virtual void ms_handle_fast_accept(Connection *con) override
    {
        cout << __func__ << " nref:" << con->get_nref() << '\n';
        return;
    }

    virtual bool ms_handle_reset(Connection *con) override
    {
        cout << __func__ << " nref:" << con->get_nref() << '\n';
        return true;
    }

    virtual void ms_handle_remote_reset(Connection *con) override
    {
        cout << __func__ << " nref:" << con->get_nref() << '\n';
    }

    virtual bool ms_verify_authorizer(Connection *con, int peer_type,             
                    int protocol, bufferlist& authorizer, bufferlist& authorizer_reply,
                    bool& isvalid, CryptoKey& session_key) override
    {
        isvalid = true;
        return true;
    }
};

int main()
{
    cct.conf_->log_level[msgr_subsys_] = 0;
    cct.conf_->log_level[msgr_subsys_ms] = 0;

    SrvDispatcher d(&cct); 

    Messenger *msgr = Messenger::create(&cct, "async",
                           entity_name_t::OSD(), "server",  0);
    if (msgr == NULL) {
        cerr << "can not create messenger\n";
        exit(EXIT_FAILURE);
    }

    msgr->set_policy(entity_name_t::TYPE_CLIENT, Messenger::Policy::stateless_server(MSGR_FEATURES_SUPPORTED_DEFAULT, MSGR_FEATURES_SUPPORTED_DEFAULT));
    entity_addr_t addr;
    if (!addr.parse("0.0.0.0:8086")) {
        cerr << "can not parse addr\n";
        exit(EXIT_FAILURE);
    }

    if (msgr->bind(addr)) {
        cerr << "can not bind addr\n";
        exit(EXIT_FAILURE);
    }

    msgr->add_dispatcher_tail(&d);
    if (msgr->start()) {
        cerr << "can not start msgr\n";
        exit(EXIT_FAILURE);
    }
    getchar();
    msgr->shutdown();
    msgr->wait();

    delete msgr;
    std::cout << "msgs: " << msgr::Message::get_total_alloc() << "\n";
    std::cout << "conns: " << msgr::Connection::get_total_alloc() << "\n";
    return 0;
}
