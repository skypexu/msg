#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <err.h>

#include "msgr/Messenger.h"
#include "msgr/dout.h"

#include "sample_msg.h"

using namespace std;
using namespace msgr;
MsgrContext cct, *g_context = &cct;

MessageFactory<SAMPLE_MSG_1, SampleMsg1> factory1;

#define dout_subsys msgr_subsys_ms

class ClientDispatcher: public Dispatcher
{
public:
    ClientDispatcher(MsgrContext *cct)
        :Dispatcher(cct)
    {
    }

    virtual bool ms_dispatch(Message *m) override
    {
        switch(m->get_type()) {
        case SAMPLE_MSG_1: {
            bufferlist::iterator it;
            cout << __func__ << "got samplemsg data length" << m->get_data().length() << '\n';
            for (it = m->get_data().begin(); !it.end(); ++it)
               cout << *it;
            }
            m->put();
            return true;
        }
        return false;
    }

    virtual void ms_handle_connect(Connection *con) override
    {
        cout << __func__ << "\n";
    }

    virtual bool ms_handle_reset(Connection *con) override
    {
        cout << __func__ << "\n";
        return false;
    }

    virtual void ms_handle_remote_reset(Connection *con) override
    {
        cout << __func__ << "\n";
    }

    virtual bool ms_verify_authorizer(Connection *con, int peer_type,             
                    int protocol, bufferlist& authorizer, bufferlist& authorizer_reply,
                    bool& isvalid, CryptoKey& session_key)
     {
        isvalid = true;
        return true;
     }   

};

int main()
{
    cct.conf_->log_level[msgr_subsys_] = 0;
    cct.conf_->log_level[msgr_subsys_ms] = 0;
    cct.conf_->ms_initial_backoff = 1;
    ClientDispatcher d(&cct);
    Messenger *msgr = Messenger::create(&cct, "async",
                           entity_name_t::CLIENT(), "client",  1);
    if (msgr == NULL) {
        cerr << "can not create messenger\n";
        exit(EXIT_FAILURE);
    }
    msgr->set_policy(entity_name_t::TYPE_OSD,
        Messenger::Policy::lossless_client(MSGR_FEATURES_SUPPORTED_DEFAULT,
            MSGR_FEATURES_SUPPORTED_DEFAULT));
    msgr->add_dispatcher_tail(&d);

    entity_addr_t addr;
    if (!addr.parse("0.0.0.0:8086")) {
        cerr << "can not parse addr\n";
        exit(EXIT_FAILURE);
    }

    entity_inst_t inst(entity_name_t::OSD(), addr);

    if (msgr->start()) {
        cerr << "can not start msgr\n";
        exit(EXIT_FAILURE);
    }

    int fd;

    fd = open("data", O_RDONLY);
    if (fd == -1)
        err(1, "open()");
    ConnectionRef conn = msgr->get_connection(inst);
    SampleMsg1 *m = new SampleMsg1;
    m->value_ = "hello";
    bufferptr ptr = buffer::create_zero_copy(1024, fd, NULL);
    bufferlist list;
    list.push_back(ptr);
    m->set_data(list);
    conn->send_message(m);
    cout << "press a key to shutdown\n";
    getchar();
    msgr->shutdown();
    msgr->wait();
    delete msgr;

    std::cout << "msgs: " << msgr::Message::get_total_alloc() << "\n";
    std::cout << "conns: " << msgr::Connection::get_total_alloc() << "\n";
    return 0;
}
