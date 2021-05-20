#ifndef ATOMIC_6495132b5af74880bd22c5cbf425eede_H
#define ATOMIC_6495132b5af74880bd22c5cbf425eede_H

#include "Spinlock.h"

namespace msgr {

#if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3) || __GNUC__ > 4) 
template <typename T>
class gcc_atomic_count {
    volatile T val;
public:
    gcc_atomic_count(): val(0)
    {
    }

    explicit gcc_atomic_count(T i)
      : val(i)
    {
    }

    gcc_atomic_count(const gcc_atomic_count<T> &other)
    {
        set(other.read());
    }

    gcc_atomic_count &operator=(const gcc_atomic_count<T> &rhs)
    {
        set(rhs.read());
        return *this;
    }

    ~gcc_atomic_count()
    {
    }

    void set(T v)
    {
#ifdef __x86_64__
        /*
         * On x86 Reads are not reordered with other reads.
         * Writes are not reordered with older reads.
         */
        __asm __volatile(" " : : : "memory");
        val = v;
#else
        T old;
        do
        {
            old = val;
        } while (!__sync_bool_compare_and_swap(&val, old, v));
#endif
    }

    T inc()
    {
        return __sync_add_and_fetch(&val, 1);
    }

    T dec()
    {
        return __sync_sub_and_fetch(&val, 1);
    }

    void add(T d)
    {
        __sync_add_and_fetch(&val, d);
    }

    void sub(T d)
    {
        __sync_sub_and_fetch(&val, d);
    }

    T read() const
    {
#ifdef __x86_64__
        /*
         * On x86 writes are not reordered with other reads and writes.
         */
        __asm __volatile(" " : : : "memory");
#else
        __sync_synchronize();
#endif
        return val;
    }
};

#endif // __GNUC__

template <class T>
class spinlock_atomic_count
{
    Spinlock lock;
    T val;
public:
    spinlock_atomic_count(): val(0)
    {
    }

    explicit spinlock_atomic_count(T i)
      : val(i)
    {
    }

    spinlock_atomic_count(const spinlock_atomic_count<T> &other)
    {
        set(other.read());
    }

    spinlock_atomic_count &operator=(const spinlock_atomic_count<T> &rhs)
    {
        set(rhs.read());
        return *this;
    }

    ~spinlock_atomic_count()
    {
    }

    void set(T v)
    {
        Spinlock::Locker locker(lock);
        val = v;
    }

    T inc()
    {
        Spinlock::Locker locker(lock);
        return ++val;
    }

    T dec()
    {
        Spinlock::Locker locker(lock);
        return --val;
    }

    void add(T d)
    {
        Spinlock::Locker locker(lock);
        val += d;
    }

    void sub(T d)
    {
        Spinlock::Locker locker(lock);
        val -= d;
    }

    T read() const
    {
        T ret;
        Spinlock::Locker locker(lock);
        ret = val;
        return ret;
    }
}; // class spinlock_atomic_count

template <typename T>
class atomic_count: public spinlock_atomic_count<T> {
public:
    atomic_count()
    {
    }

    explicit atomic_count(T i)
    : spinlock_atomic_count<T>(i)
    {
    }
};

#if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3) || __GNUC__ > 4)

#define INSTANTIATE_ATOMIC_T_(T)                    \
template <>                                         \
class atomic_count<T> : public gcc_atomic_count<T>  \
{                                                   \
public:                                             \
    atomic_count()                                  \
    {                                               \
    }                                               \
                                                    \
    explicit atomic_count(T i)                      \
    : gcc_atomic_count<T>(i)                        \
    {                                               \
    }                                               \
}

INSTANTIATE_ATOMIC_T_(int);
INSTANTIATE_ATOMIC_T_(unsigned);
#ifdef __x86_64__
INSTANTIATE_ATOMIC_T_(short);
INSTANTIATE_ATOMIC_T_(unsigned short);
INSTANTIATE_ATOMIC_T_(long);
INSTANTIATE_ATOMIC_T_(unsigned long);
typedef atomic_count<long> atomic_t;
#else
typedef atomic_count<int> atomic_t;
#endif // __x86_64__

#else

typedef atomic_count<int> atomic_t;

#endif // __GNUC__

} // namespace msgr

#endif  // ATOMIC_6495132b5af74880bd22c5cbf425eede_H
