#include <iostream>

#include "msgr/atomic.h"
#include "msgr/types.h"
#include "msgr/Message.h"
#include "msgr/MsgrConfig.h"
#include "msgr/environment.h"

using namespace std;

#define dout_subsys msgr_subsys_ms

namespace {
    msgr::atomic_count<unsigned> message_total_alloc;
    bool message_track_alloc = get_env_bool("MSGR_MESSAGE_TRACK");
}

namespace msgr {

void Message::inc_total_alloc()
{                                                                               
    if (message_track_alloc)
        message_total_alloc.inc();
}                                                                               

void Message::dec_total_alloc()
{
    if (message_track_alloc)
      message_total_alloc.dec();
}
                                                                                
unsigned Message::get_total_alloc()
{
    return message_total_alloc.read();
}

void Message::encode(uint64_t features, int crcflags)
{
    if (empty_payload()) {
        encode_payload(features);

        if (header.compat_version == 0)
            header.compat_version = header.version;
    }

    if (crcflags & MSG_CRC_HEADER)
        calc_front_crc();

    // update envelope
    header.front_len = get_payload().length();
    header.middle_len = get_middle().length();
    header.data_len = get_data().length();

    if (crcflags & MSG_CRC_HEADER)
        calc_header_crc();

    footer.flags = MSGR_MSG_FOOTER_COMPLETE;
    if (crcflags & MSG_CRC_DATA) {
        calc_data_crc();
#ifdef ENCODE_DUMP
        bufferlist bl;
        ::encode(get_header(), bl);

        ::encode(footer, bl);

        ::encode(get_payload(), bl);
        ::encode(get_middle(), bl);
        ::encode(get_data(), bl);

        // this is almost an exponential backoff, except because we count
        // bits we tend to sample things we encode later, which should be
        // more representative.
        static int i = 0;
        i++;
        int bits = 0;
        for (unsigned t = i; t; bits++)
            t &= t - 1;
        if (bits <= 2) {
            char fn[200];
            int status;
            snprintf(fn, sizeof(fn), ENCODE_STRINGIFY(ENCODE_DUMP) "/%s__%d.%x",
                abi::__cxa_demangle(typeid(*this).name(), 0, 0, &status),
                getpid(), i++);
            int fd = ::open(fn, O_WRONLY|O_TRUNC|O_CREAT, 0644);
            if (fd >= 0) {
                bl.write_fd(fd);
                ::close(fd);
            }
        }
#endif
    } else {
        footer.flags = (unsigned)footer.flags | MSGR_MSG_FOOTER_NOCRC;
    }
}

Message *decode_message(MsgrContext* pct, int crcflags,
    msgr_msg_header& header,
    msgr_msg_footer& footer, bufferlist& front,
    bufferlist& middle, bufferlist& data)
{
    // verify crc
    if (crcflags & MSG_CRC_HEADER) {
        uint32_t front_crc = front.crc32c(0);
        uint32_t middle_crc = middle.crc32c(0);

        if (front_crc != footer.front_crc) {
            if (pct) {
                ldout(pct, 0) << "bad crc in front " << front_crc << " != exp " << footer.front_crc << dendl;
                ldout(pct, 20) << " ";
                front.hexdump(*dout_);
                *dout_ << dendl;
            }
            return 0;
        }
        if (middle_crc != footer.middle_crc) {
            if (pct) {
                ldout(pct, 0) << "bad crc in middle " << middle_crc << " != exp " << footer.middle_crc << dendl;
                ldout(pct, 20) << " ";
                middle.hexdump(*dout_);
                *dout_ << dendl;
            }
            return 0;
        }
    }

    if (crcflags & MSG_CRC_DATA) {
        if ((footer.flags & MSGR_MSG_FOOTER_NOCRC) == 0) {
            uint32_t data_crc = data.crc32c(0);
            if (data_crc != footer.data_crc) {
                if (pct) {
                    ldout(pct, 0) << "bad crc in data " << data_crc << " != exp " << footer.data_crc << dendl;
                    ldout(pct, 20) << " ";
                    data.hexdump(*dout_);
                    *dout_ << dendl;
                }
                return 0;
            }
        }
    }

    // make message
    int type = header.type;
    Message *m = MessageFactoryRegistry::make_message(type);
    if (m == NULL)
      return NULL;
    m->set_context(pct);

    if (m->get_header().version &&
        m->get_header().version < header.compat_version) {
        ldout(pct, 0)
            << "will not decode message of type " << type
            << " version " << header.version
            << " because compat_version " << header.compat_version
            << " > supported version " << m->get_header().version << dendl;
        if (pct->conf_->ms_die_on_bad_msg)
            assert(0);
        m->put();
        return 0;
    }

    m->set_header(header);
    m->set_payload(front);
    m->set_middle(middle);
    m->set_data(data);

    try {
        m->decode_payload();
    } catch (const buffer::error &e) {
        if (pct) {
            lderr(pct) << "failed to decode message of type " << type
                       << " v" << header.version
                       << ": " << e.what() << dendl;
            ldout(pct, pct->conf_->ms_dump_corrupt_message_level) << "dump: \n";
                  m->get_payload().hexdump(*dout_);
            *dout_ << dendl;
            if (pct->conf_->ms_die_on_bad_msg)
                assert(0);
        }
        m->put();
        return 0;
    }

    //done !
    return m;
}

Message *decode_message(MsgrContext *pct, int crcflags,
         msgr_msg_header& header,
         msgr_msg_footer& footer, bufferptr& front,
         bufferptr& middle, bufferptr& data)
{
    bufferlist frontlist;
    bufferlist middlelist;
    bufferlist datalist;

    if (front.length())
        frontlist.push_back(front);
    if (middle.length())
        middlelist.push_back(middle);
    if (data.length())
        datalist.push_back(data);

    return decode_message(pct, crcflags, header, footer, frontlist, middlelist,
            datalist);
}

/////////////////////////////////////////////////////////////////////////////
// Message factory registry

struct mf_info {
    struct mf_node {
        MessageFactoryRegistry::make_message_func_t func;
    };

    typedef std::unordered_map<int, mf_node> mf_dict;
    pthread_rwlock_t     lock_;
    mf_dict         dict_;

    mf_info()
    {
        pthread_rwlock_init(&lock_, NULL);
    }

    bool register_func(int t, MessageFactoryRegistry::make_message_func_t func)
    {
        mf_dict::iterator it;

        pthread_rwlock_wrlock(&lock_);
        it = dict_.find(t);
        if (it != dict_.end() && it->second.func != func) {
            cerr << "warning! message type :" << t << " was already registered!\n"
                 << std::flush;
            pthread_rwlock_unlock(&lock_);
            return false;
        }
        dict_[t].func = func;
        pthread_rwlock_unlock(&lock_);
        return true;
    }

    bool lookup(int type, mf_node &n)
    {
        mf_dict::const_iterator it;

        pthread_rwlock_rdlock(&lock_);
        it = dict_.find(type);
        if (it == dict_.end()) {
            pthread_rwlock_unlock(&lock_);
            return false;
        }
        n = it->second;
        pthread_rwlock_unlock(&lock_);
        return true;
    }

    Message *make_message(int type)
    {
        mf_node n;
        if (lookup(type, n))
            return n.func(type);
        return NULL;
    }
};

static mf_info        *mf_info_;
static pthread_once_t  mf_once_;
static void mf_init() { mf_info_ = new mf_info; }

#define MF_INIT()       pthread_once(&mf_once_, mf_init)

Message *MessageFactoryRegistry::make_message(int type)
{
    MF_INIT();
    return mf_info_->make_message(type);
}

bool MessageFactoryRegistry::register_func(int t,
        MessageFactoryRegistry::make_message_func_t func)
{
    MF_INIT();
    return mf_info_->register_func(t, func);
}

} // namespace msgr
