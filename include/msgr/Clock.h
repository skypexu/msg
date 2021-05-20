#ifndef CLOCK_bfc63ddf7aca4328a548c940d6ec92c5_H
#define CLOCK_bfc63ddf7aca4328a548c940d6ec92c5_H

#include <time.h>

#include "MsgrContext.h"
#include "utime.h"

extern msgr::utime_t msgr_clock_now(msgr::MsgrContext *cct);
extern time_t msgr_clock_gettime(msgr::MsgrContext *cct);

#endif
