#ifndef MSGR_ASSERT_7e288bc3fc9b_H
#define MSGR_ASSERT_7e288bc3fc9b_H

#include <assert.h>
#include <stdio.h>

/*
 * make sure we don't try to use the reserved features
 */
#define MSGR_STATIC_ASSERT(x) (void)(sizeof(int[((x)==0) ? -1 : 0]))

void msgr_panic(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));

#define msgr_assert(exp,msg) do {                       \
    if (!(exp))                                          \
        msgr_panic msg;                                 \
} while (0)

void msgr_print_stacktrace(FILE *out, unsigned int max_frames);

#endif // MSGR_ASSERT_7e288bc3fc9b_H
