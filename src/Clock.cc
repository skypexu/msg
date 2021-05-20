#include <time.h>

#include "msgr/MsgrContext.h"
#include "msgr/MsgrConfig.h"
#include "msgr/utime.h"
#include "msgr/Clock.h"

using namespace msgr;
utime_t msgr_clock_now(MsgrContext *cct)
{
#if defined(__linux__)
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    utime_t n(tp);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    utime_t n(&tv);
#endif
    if (cct)
        n += cct->conf_->clock_offset;
    return n;
}

time_t msgr_clock_gettime(MsgrContext *cct)
{
    time_t ret = time(NULL);
    if (cct)
        ret += ((time_t)cct->conf_->clock_offset);
    return ret;
}
