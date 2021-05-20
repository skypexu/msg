#ifndef SPINLOCK_22bc0633cf8b464aa0a42a1d18463d05_H
#define SPINLOCK_22bc0633cf8b464aa0a42a1d18463d05_H

#include <pthread.h>

namespace msgr {

class Spinlock {
    mutable pthread_spinlock_t lock_;

    // don't allow copying.
    void operator = (Spinlock& s);
    Spinlock(const Spinlock& s);

public:
    Spinlock()
    {
        pthread_spin_init(&lock_, PTHREAD_PROCESS_PRIVATE);
    }

    ~Spinlock()
    {
        pthread_spin_destroy(&lock_);
    }

    /// acquire spinlock
    void lock() const
    {
        pthread_spin_lock(&lock_);
    }

    /// release spinlock
    void unlock() const
    {
        pthread_spin_unlock(&lock_);
    }

    class Locker {
        const Spinlock& spinlock;
    public:
        Locker(const Spinlock& s)
         : spinlock(s)
        {
            spinlock.lock();
        }

        ~Locker()
        {
            spinlock.unlock();
        }
    };
};

} // namespace msgr
#endif // SPINLOCK_22bc0633cf8b464aa0a42a1d18463d05_H
