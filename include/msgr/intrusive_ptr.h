#ifndef INTRUSIVE_PTR_7907b38fdad0_H
#define INTRUSIVE_PTR_7907b38fdad0_H

#include <stddef.h>
#include <iostream>

namespace msgr
{

// A reference-counted pointer which stores the reference count inside the
// pointer.  This requires you to have "void inc()" and "void dec()" functions
// which increment and decrement the reference count.  If the reference count
// is decremented to 0, the object should delete itself.
//
// It is the explicit goal of this implementation to enable copies of the
// intrusive pointer to be created and destroyed without requiring any
// additional form of synchronization.  Specifically, as long as the
// intrusive_ptr<T> that is the source of a copy operation remains valid
// throughout the entire copy operation, there is no need for additional
// synchronization.
//
// Usage tips for maximum safety:
//  - The reference count should start at 0 as it will be incremented.
//  - intrusive_ptr<T> must be a friend of T, or at least be able to call
//    inc/dec.
//  - Only use the intrusive_ptr<T>(T*) constructor for the initial
//    intrusive_ptr.  All copies thereafter should come from the copy
//    constructor and assignment operators.
//  - Make sure that the intrusive_ptr<T> you are copying from remains valid
//    throughout the copy.  This means don't do anything like copying from an
//    intrusive_ptr<T> you are going to delete from another thread.
//  - Destructors should never throw exceptions.  The throw specifiers in here
//    assume that this convention is adhered to.
//  - Inc/Dec need to bring their own synchronization around the reference
//    count.

template <typename T>
class intrusive_ptr
{
    typedef intrusive_ptr this_type;
    typedef T* intrusive_ptr<T>::*bool_type;

public:
    typedef T element_type;

    intrusive_ptr() throw ()
        : m_ptr(0)
    {
    }

    intrusive_ptr(const intrusive_ptr<T>& rhs) throw ()
        : m_ptr(rhs.m_ptr)
    {
        if (m_ptr)
            intrusive_ptr_add_ref(m_ptr);
    }

    intrusive_ptr(T* ptr) throw ()
        : m_ptr(ptr)
    {
        if (m_ptr)
            intrusive_ptr_add_ref(m_ptr);
    }

    intrusive_ptr(T* ptr, bool add_ref ) throw ()
        : m_ptr(ptr)
    {
        if (m_ptr && add_ref)
            intrusive_ptr_add_ref(m_ptr);
    }

    template<class Y> intrusive_ptr(intrusive_ptr<Y> const & rhs)
        : m_ptr(rhs.ptr())
    {
        if (m_ptr)
            intrusive_ptr_add_ref(m_ptr);
    }

    ~intrusive_ptr() throw ()
    {
        if (m_ptr)
            intrusive_ptr_release(m_ptr);
    }

    intrusive_ptr & operator=(intrusive_ptr const & rhs)                        
    {                                                                           
        this_type(rhs).swap(*this);
        return *this;                                                           
    }

    template<class U> intrusive_ptr & operator=(intrusive_ptr<U> const & rhs)
    {
        this_type(rhs).swap(*this);
        return *this;
    }

    intrusive_ptr & operator=(T * rhs)                                          
    {                                                                           
        this_type(rhs).swap(*this);
        return *this;                                                           
    }

    void reset() throw ()
    {
        if (m_ptr)
            intrusive_ptr_release(m_ptr);
        m_ptr = 0;
    }

    void reset(T *rhs) throw ()
    {
       this_type( rhs ).swap( *this );
    }

    void reset(T * rhs, bool add_ref)
    {
       this_type( rhs, add_ref ).swap( *this );
    }

    T* ptr() const throw ()
    {
        return m_ptr;
    }

    T * detach() throw()
    {
        T *tmp = m_ptr;
        m_ptr = 0;
        return tmp;
    }

    T&
    operator * () const throw()
    {
        return *m_ptr;
    }

    T*
    operator -> () const throw()
    {
        return m_ptr;
    }

    operator bool_type () const throw()
    {
         return m_ptr == 0 ? 0 : &intrusive_ptr<T>::m_ptr;
    }

    void
    swap(intrusive_ptr<T>& other)
    {
        T *tmp = m_ptr;
        m_ptr = other.m_ptr;
        other.m_ptr = tmp;
    }

private:
    T* m_ptr;
};

template<class T, class U> inline bool operator==(intrusive_ptr<T> const & a, intrusive_ptr<U> const & b)
{
    return a.ptr() == b.ptr();
}                                                                               
                                                                                
template<class T, class U> inline bool operator!=(intrusive_ptr<T> const & a, intrusive_ptr<U> const & b)
{
    return a.ptr() != b.ptr();
}                                                                               
                                                                                
template<class T, class U> inline bool operator==(intrusive_ptr<T> const & a, U * b)
{
    return a.ptr() == b;
}
                                                                                
template<class T, class U> inline bool operator!=(intrusive_ptr<T> const & a, U * b)
{
    return a.ptr() != b;
}
                                                                                
template<class T, class U> inline bool operator==(T * a, intrusive_ptr<U> const & b)
{
    return a == b.ptr();
}
                                                                                
template<class T, class U> inline bool operator!=(T * a, intrusive_ptr<U> const & b)
{
    return a != b.ptr();
}

#if __GNUC__ == 2 && __GNUC_MINOR__ <= 96                                       

// Resolve the ambiguity between our op!= and the one in rel_ops

template<class T> inline bool operator!=(intrusive_ptr<T> const & a, intrusive_ptr<T> const & b)
{
    return a.ptr() != b.ptr();
}
                                                                                
#endif    

template<class T> inline bool operator<(intrusive_ptr<T> const & a, intrusive_ptr<T> const & b)
{
    return std::less<T *>()(a.ptr(), b.ptr());
}                                                                               

template<class T> void swap(intrusive_ptr<T> & lhs, intrusive_ptr<T> & rhs)
{                                                                               
    lhs.swap(rhs);
}                      

// mem_fn support                                                               
                                                                                
template<class T> T * get_pointer(intrusive_ptr<T> const & p)
{
    return p.ptr();
}

template<class T, class U>
intrusive_ptr<T> static_pointer_cast(intrusive_ptr<U> const & r) // never throws
{
    return intrusive_ptr<T>(static_cast<T*>(r.ptr()));
}

template<class T, class U>
intrusive_ptr<T> const_pointer_cast(intrusive_ptr<U> const & r) // never throws
{
    return intrusive_ptr<T>(const_cast<T*>(r.ptr()));
}

template<class T, class U>
  intrusive_ptr<T> dynamic_pointer_cast(intrusive_ptr<U> const & r)
{
    return intrusive_ptr<T>(dynamic_cast<T*>(r.ptr()));
}

} // namespace msgr

namespace std {

// operator<< 
template<class E, class T, class Y>
std::basic_ostream<E, T> & operator<< (std::basic_ostream<E, T> & os, msgr::intrusive_ptr<Y> const & p)
{
    os << p.ptr();
    return os;
}

template <typename T>
std::ostream& operator << (std::ostream& lhs, const msgr::intrusive_ptr<T>& rhs)
{
    lhs << rhs.ptr();
    return lhs;
}

}

#endif // INTRUSIVE_PTR_7907b38fdad0_H
