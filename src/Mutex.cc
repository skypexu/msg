#include <iostream>
#include <string.h>

#include "msgr/Mutex.h"

namespace msgr {

Mutex::Mutex(const std::string &n, bool r)
 : name(n), id(-1), recursive(r), nlock(0), locked_by(0), locked_by_name(0)
{
    if (recursive) {
        // Mutexes of type PTHREAD_MUTEX_RECURSIVE do all the same checks as
        // mutexes of type PTHREAD_MUTEX_ERRORCHECK.
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&_m,&attr);
        pthread_mutexattr_destroy(&attr);
    }
    else {
        // If the mutex type is PTHREAD_MUTEX_NORMAL, deadlock detection
        // shall not be provided. Attempting to relock the mutex causes
        // deadlock. If a thread attempts to unlock a mutex that  it  has not
        // locked or a mutex which is unlocked, undefined behavior results.
        pthread_mutex_init(&_m, NULL);
    }
}

Mutex::~Mutex() {
    pthread_mutex_destroy(&_m);
    // pthread_mutex_destroy may return EBUSY without doing anything!
    // However we want to let lock holder know there is a race condition
    // between the destructor and other threads (lock and unlock).
#ifdef __linux__
    // Set to an invalid value.
    _m.__data.__kind = -1;
#else
    memset(&_m, 0, sizeof(_m));
#endif

#ifndef NDEBUG
    if (nlock != 0) {
        std::cerr << "mutex: " << name << " nlock: " << nlock << " locked by: " << locked_by_name << "\n" << std::flush;
        //assert(nlock == 0);
        abort();
    }
#endif
}

void Mutex::Lock() {
    int r;

    r = pthread_mutex_lock(&_m);
    if (r != 0) {
        std::cerr << "error in Mutex::Lock " << name << "\n";
        assert(r == 0);
    }
    r = r;
    _post_lock();
}

void Mutex::Unlock() {
    _pre_unlock();
    int r = pthread_mutex_unlock(&_m);
    if (r != 0) {
        std::cerr << "error in Mutex::Unlock " << name << "\n";
        assert(r == 0);
    }
    r = r;
}

} // namespace msgr
