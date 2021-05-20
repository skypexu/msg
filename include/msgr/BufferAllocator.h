#ifndef BUFFERALLOCATOR_002e3e1364bc_H
#define BUFFERALLOCATOR_002e3e1364bc_H

#include "msgr/buffer.h"

namespace msgr {

class MsgrContext;

class BufferAllocator {
public:
    MsgrContext *pct_;

    BufferAllocator(MsgrContext *pct) { pct_ = pct; }
    virtual ~BufferAllocator() {}

    virtual int allocate_msg_payload(int type, unsigned int len, bufferptr &ptr) = 0;
    virtual int allocate_msg_middle(int type, unsigned int len, bufferptr &ptr) = 0;
    virtual int allocate_msg_data(int type, unsigned int off, unsigned len, bufferlist &list) = 0;
};

class DefaultBufferAllocator : public BufferAllocator {
public:
    DefaultBufferAllocator(MsgrContext *pct) : BufferAllocator(pct) {}
    ~DefaultBufferAllocator() {}

    virtual int allocate_msg_payload(int type, unsigned int len, bufferptr &ptr) override;
    virtual int allocate_msg_middle(int type, unsigned int len, bufferptr &ptr) override;
    virtual int allocate_msg_data(int type, unsigned int off, unsigned len, bufferlist &list) override;
};

}

#endif // BUFFERALLOCATOR_002e3e1364bc_H
