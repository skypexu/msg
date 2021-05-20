#ifndef MSGR_TIMESPEC_626cf4ef0ca64e48ab81735c47394ed6_H
#define MSGR_TIMESPEC_626cf4ef0ca64e48ab81735c47394ed6_H

namespace msgr {
    struct msgr_timespec {
        __le32 tv_sec;
        __le32 tv_nsec;
    } __attribute__ ((packed));

    WRITE_RAW_ENCODER(msgr_msg_header)
}

#endif // MSGR_TIMESPEC_626cf4ef0ca64e48ab81735c47394ed6_H
