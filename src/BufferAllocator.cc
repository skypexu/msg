#include "msgr/page.h"
#include "msgr/BufferAllocator.h"

namespace msgr {

int DefaultBufferAllocator::allocate_msg_payload(int type, unsigned len, bufferptr &ptr)
{
    ptr = buffer::create(len);
    return 0;
}

int DefaultBufferAllocator::allocate_msg_middle(int type, unsigned int len, bufferptr &ptr)
{
    ptr = buffer::create(len);
    return 0;
}

int DefaultBufferAllocator::allocate_msg_data(int type, unsigned int off, unsigned len, bufferlist &data)
{
    // create a buffer to read into that matches the data alignment
    unsigned left = len;
    if (off & ~MSGR_PAGE_MASK) {
        // head
        unsigned head = 0;
        head = std::min(MSGR_PAGE_SIZE - (off & ~MSGR_PAGE_MASK), (unsigned long)left);
        bufferptr bp = buffer::create(head);
        data.push_back(bp);
        left -= head;
    }
#if 0
    unsigned middle = left & MSGR_PAGE_MASK;
    if (middle > 0) {
        bufferptr bp = buffer::create_page_aligned(middle);
        data.push_back(bp);
        left -= middle;
    }
    if (left) {
        bufferptr bp = buffer::create(left);
        data.push_back(bp);
    }
#else
    if (left) {
        bufferptr bp = buffer::create_page_aligned(left);
        data.push_back(bp);
    }
#endif

    return 0;
}

}
