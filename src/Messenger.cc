#include "msgr/types.h"
#include "msgr/environment.h"
#include "msgr/Messenger.h"
#include "msgr/BufferAllocator.h"
#include "async/AsyncMessenger.h"

namespace {
    msgr::atomic_count<unsigned> msgr_total_alloc;
    bool msgr_track_alloc = get_env_bool("MSGR_MESSENGER_TRACK");
}

namespace msgr {

Messenger::~Messenger()                                                          
{                                                                             
    dec_total_alloc();
    delete buf_allocator;
}

void Messenger::inc_total_alloc()
{
    if (msgr_track_alloc)
        msgr_total_alloc.inc();
}

void Messenger::dec_total_alloc()
{
   if (msgr_track_alloc)
      msgr_total_alloc.dec();
}

int Messenger::get_total_alloc()
{
    return msgr_total_alloc.read();
}

void Messenger::set_buffer_allocator(BufferAllocator *allocator)
{
    delete buf_allocator;
    buf_allocator = allocator;
}

void Messenger::set_default_buffer_allocator()
{
    set_buffer_allocator(new DefaultBufferAllocator(cct));
}

Messenger *Messenger::create(MsgrContext *pct, const std::string &type,
			     entity_name_t name, std::string mname,
			     uint64_t nonce)
{
    if (type == "async")
        return new AsyncMessenger(pct, name, mname, nonce);
    lderr(pct) << "unrecognized ms_type '" << type << "'" << dendl;
    return NULL;
}

/*
 * Pre-calculate desired software CRC settings.  CRC computation may
 * be disabled by default for some transports (e.g., those with strong
 * hardware checksum support).
 */
int Messenger::get_default_crc_flags(MsgrConfig * conf)
{
    int r = 0;
    if (conf->ms_crc_data)
        r |= MSG_CRC_DATA;
    if (conf->ms_crc_header)
        r |= MSG_CRC_HEADER;
    return r;
}

} //namespace msgr
