#ifndef SAMPLE_MSG_H
#define SAMPLE_MSG_H

#include "msgr/Message.h"

using namespace msgr;

#define SAMPLE_MSG_1 0x1000

class SampleMsg1 : public Message {
public:
    std::string value_;

    SampleMsg1() : Message(SAMPLE_MSG_1) {}
    ~SampleMsg1() {}

    const char *get_type_name() const { return "SampleMsg1"; }
    void encode_payload(uint64_t features)
    {
        msgr::encode(value_, payload);
    }

    void decode_payload()  
    {
        bufferlist::iterator p = payload.begin(); 
        msgr::decode(value_, p); 
    } 
};

MessageFactory<SAMPLE_MSG_1, SampleMsg1> SampleMsgFactory;

#endif
