#ifndef MESSAGE_440d6616ed844b269e822391afb20dbd_H
#define MESSAGE_440d6616ed844b269e822391afb20dbd_H

#include <sys/queue.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "RefCountedObj.h"
#include "buffer.h"
#include "encoding.h"
#include "msgr.h"
#include "msg_types.h"
#include "utime.h"
#include "Connection.h"
#include "intrusive_ptr.h"
#include "Throttle.h"

// *** Message::encode() crcflags bits ***
#define MSG_CRC_DATA           (1 << 0)
#define MSG_CRC_HEADER         (1 << 1)
#define MSG_CRC_ALL            (MSG_CRC_DATA | MSG_CRC_HEADER)

namespace msgr {

class Message : public RefCountedObject {
protected:
    msgr_msg_header    header;   // headerelope
    msgr_msg_footer    footer;
    bufferlist          payload;  // "front" unaligned blob
    bufferlist          middle;   // "middle" unaligned blob
    bufferlist          data;     // data payload (page-alignment will be preserved where possible)

    /* recv_stamp is set when the Messenger starts reading the
     * Message off the wire */
    utime_t recv_stamp;
    /* dispatch_stamp is set when the Messenger starts calling dispatch() on
     * its endpoints */
    utime_t dispatch_stamp;
    /* throttle_stamp is the point at which we got throttle */
    utime_t throttle_stamp;
    /* time at which message was fully read */
    utime_t recv_complete_stamp;

    ConnectionRef connection;

    uint32_t magic;

    // release our size in bytes back to this throttler when our payload
    // is adjusted or when we are destroyed.
    Throttle *byte_throttler;

    // release a count back to this throttler when we are destroyed
    Throttle *msg_throttler;

    // keep track of how big this message was when we reserved space in
    // the msgr dispatch_throttler, so that we can properly release it
    // later.  this is necessary because messages can enter the dispatch
    // queue locally (not via read_message()), and those are not
    // currently throttled.
    uint64_t dispatch_throttle_size;

    friend class Messenger;

    /* hack for memory utilization debugging. */
    static void inc_total_alloc();
    static void dec_total_alloc();

public:

    /// total messages allocated                                                     
    static unsigned get_total_alloc();                                                 
                                                                                
    Message()
      : connection(NULL),
        magic(0),
        byte_throttler(NULL),                                                     
        msg_throttler(NULL),                                                      
        dispatch_throttle_size(0)
    {
        inc_total_alloc();
        memset(&header, 0, sizeof(header));
        memset(&footer, 0, sizeof(footer));
    }

    Message(int t, int version=1, int compat_version=0)
      : connection(NULL),
        magic(0),
        byte_throttler(NULL),
        msg_throttler(NULL),
        dispatch_throttle_size(0)
    {
        inc_total_alloc();
        memset(&header, 0, sizeof(header));
        header.type = t;
        header.version = version;
        header.compat_version = compat_version;
        header.priority = 0;  // undef
        header.data_off = 0;
        memset(&footer, 0, sizeof(footer));
    }

    Message *get()
    {
        return static_cast<Message *>(RefCountedObject::get());
    }

protected:
    virtual ~Message()
    {
        dec_total_alloc();
        if (byte_throttler)
            byte_throttler->put(dispatch_throttle_size);
        if (msg_throttler)
            msg_throttler->put();
        /* call completion hooks (if any) */
//        if (completion_hook)
//            completion_hook->complete(0);
    }

public:
    inline const ConnectionRef& get_connection() const { return connection; }
    void set_connection(const ConnectionRef& c) { connection = c; }
    void set_byte_throttler(Throttle *t) { byte_throttler = t; }
    Throttle *get_byte_throttler() { return byte_throttler; }
    void set_message_throttler(Throttle *t) { msg_throttler = t; }
    Throttle *get_message_throttler() { return msg_throttler; }
    void set_dispatch_throttle_size(uint64_t s) { dispatch_throttle_size = s; }
    uint64_t get_dispatch_throttle_size() { return dispatch_throttle_size; }
    msgr_msg_header &get_header() { return header; }
    void set_header(const msgr_msg_header &e) { header = e; }
    void set_footer(const msgr_msg_footer &e) { footer = e; }
    msgr_msg_footer &get_footer() { return footer; }
    void set_src(const entity_name_t& src) { header.src = src; }

    uint32_t get_magic() { return magic; }
    void set_magic(int _magic) { magic = _magic; }

    /*
     * If you use get_[data, middle, payload] you shouldn't
     * use it to change those bufferlists unless you KNOW
     * there is no throttle being used. The other
     * functions are throttling-aware as appropriate.
     */
    void clear_payload()
    {
        payload.clear();
        middle.clear();
    }

    virtual void clear_buffers() {}

    void clear_data() {
        data.clear();
        clear_buffers(); // let subclass drop buffers as well
    }

    bool empty_payload() { return payload.length() == 0; }
    bufferlist& get_payload() { return payload; }
    void set_payload(bufferlist& bl)
    {
        payload.claim(bl, buffer::list::CLAIM_ALLOW_NONSHAREABLE);
    }

    void set_middle(bufferlist& bl)
    {
        middle.claim(bl, buffer::list::CLAIM_ALLOW_NONSHAREABLE);
    }

    bufferlist& get_middle() { return middle; }

    void set_data(const bufferlist &bl) { data.share(bl); }
    bufferlist& get_data() { return data; }
    off_t get_data_len() { return data.length(); }

    void set_recv_stamp(utime_t t) { recv_stamp = t; }
    const utime_t& get_recv_stamp() const { return recv_stamp; }
    void set_dispatch_stamp(utime_t t) { dispatch_stamp = t; }
    const utime_t& get_dispatch_stamp() const { return dispatch_stamp; }
    void set_throttle_stamp(utime_t t) { throttle_stamp = t; }
    const utime_t& get_throttle_stamp() const { return throttle_stamp; }
    void set_recv_complete_stamp(utime_t t) { recv_complete_stamp = t; }
    const utime_t& get_recv_complete_stamp() const { return recv_complete_stamp; }

    void calc_header_crc() {
        header.crc = msgr_crc32c(0, (unsigned char*)&header,
                 sizeof(header) - sizeof(header.crc));
    }

    void calc_front_crc() {
        footer.front_crc = payload.crc32c(0);
        footer.middle_crc = middle.crc32c(0);
    }

    void calc_data_crc() {
        footer.data_crc = data.crc32c(0);
    }

    virtual int get_cost() const {
        return data.length();
    }

    // type
    int get_type() const { return header.type; }
    void set_type(int t) { header.type = t; }

    uint64_t get_tid() const { return header.tid; }
    void set_tid(uint64_t t) { header.tid = t; }

    unsigned get_seq() const { return header.seq; }
    void set_seq(unsigned s) { header.seq = s; }

    unsigned get_priority() const { return header.priority; }
    void set_priority(int16_t p) { header.priority = p; }

    // source/dest
    entity_inst_t get_source_inst() const {
        return entity_inst_t(get_source(), get_source_addr());
    }
    entity_name_t get_source() const {
        return entity_name_t(header.src);
    }
    entity_addr_t get_source_addr() const {
        if (connection)
            return connection->get_peer_addr();
        return entity_addr_t();
    }

    // forwarded?
    entity_inst_t get_orig_source_inst() const {
        return get_source_inst();
    }
    entity_name_t get_orig_source() const {
        return get_orig_source_inst().name;
    }
    entity_addr_t get_orig_source_addr() const {
        return get_orig_source_inst().addr;
    }

    // virtual bits
    virtual void decode_payload() = 0;
    virtual void encode_payload(uint64_t features) = 0;
    virtual const char *get_type_name() const = 0;

    virtual void print(std::ostream& out) const {
        out << get_type_name();
    }

    void encode(uint64_t features, int crcflags);
};

typedef intrusive_ptr<Message> MessageRef;

extern Message *decode_message(MsgrContext* pct, int crcflags,
                    msgr_msg_header &header,
                    msgr_msg_footer &footer,
                    bufferlist& front,
                    bufferlist& middle, bufferlist& data);
extern Message *decode_message(MsgrContext *pct, int crcflags,
                    msgr_msg_header &header,
                    msgr_msg_footer &footer,
                    bufferptr& front,
                    bufferptr& middle, bufferptr& data);

class MessageFactoryRegistry
{
public:
    typedef Message* (*make_message_func_t)(int type);

    static bool register_func(int type, make_message_func_t f);
    static Message *make_message(int type);
};

template<int Id, typename T>
class MessageFactory {
public:
    MessageFactory() {
        MessageFactoryRegistry::register_func(Id, make_message);
    }

    static Message *make_message(int type) {
        assert(type == Id);
        return new T;
    }
};

// A CRTP class to help registering message class
template<int Id, typename T>
class AutoRegMessage : public Message {
public:
    AutoRegMessage()
      : Message(Id)
    {
        *(volatile bool *)&reg_;
    }

    AutoRegMessage(int t, int version=1, int compat_version=0)
      : Message(t, version, compat_version)
    {
        *(volatile bool *)&reg_;
    }

    static Message *make_message(int type)
    {
        assert(type == Id);
        return new T;
    }

private:
    static bool init()
    { 
        return MessageFactoryRegistry::register_func(Id, make_message);
    }

    static bool reg_;
};

template<int Id, typename T>
bool AutoRegMessage<Id, T>::reg_ = AutoRegMessage<Id, T>::init();

} //namespace msgr

namespace std {

inline ostream& operator<<(ostream& out, msgr::Message& m) {
    m.print(out);
    if (m.get_header().version)
        out << " v" << m.get_header().version;
    return out;
}

} // namespace std

#endif
