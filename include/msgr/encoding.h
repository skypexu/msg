#ifndef ENCODING_9331533892514b4cbc0268ae6e4dbbca_H
#define ENCODING_9331533892514b4cbc0268ae6e4dbbca_H

#include <set>
#include <map>
#include <deque>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "byteorder.h"
#include "buffer.h"

namespace msgr {

// --------------------------------------
// base types

template<class T>
inline void encode_raw(const T& t, bufferlist& bl)
{
    bl.append((char*)&t, sizeof(t));
}
template<class T>
inline void decode_raw(T& t, bufferlist::iterator &p)
{
    p.copy(sizeof(t), (char*)&t);
}

#define WRITE_RAW_ENCODER(type)                     \
  inline void encode(const type &v, bufferlist& bl, uint64_t features=0) { encode_raw(v, bl); } \
  inline void decode(type &v, bufferlist::iterator& p) { decode_raw(v, p); }

WRITE_RAW_ENCODER(uint8_t)
WRITE_RAW_ENCODER(int8_t)
WRITE_RAW_ENCODER(char)
WRITE_RAW_ENCODER(msgr_le64)
WRITE_RAW_ENCODER(msgr_le32)
WRITE_RAW_ENCODER(msgr_le16)

// FIXME: we need to choose some portable floating point encoding here
WRITE_RAW_ENCODER(float)
WRITE_RAW_ENCODER(double)

inline void encode(const bool &v, bufferlist& bl) {
    uint8_t vv = v;
    encode_raw(vv, bl);
}

inline void decode(bool &v, bufferlist::iterator& p) {
    uint8_t vv;
    decode_raw(vv, p);
    v = vv;
}

#define WRITE_INTTYPE_ENCODER(type, etype)                              \
  inline void encode(type v, bufferlist& bl, uint64_t features=0) {     \
    msgr_##etype e;                                                    \
    e = v;                                                              \
    encode_raw(e, bl);                                                  \
  }                                                                     \
  inline void decode(type &v, bufferlist::iterator& p) {                \
    msgr_##etype e;                                                    \
    decode_raw(e, p);                                                   \
    v = e;                                                              \
  }

WRITE_INTTYPE_ENCODER(uint64_t, le64)
WRITE_INTTYPE_ENCODER(int64_t, le64)
WRITE_INTTYPE_ENCODER(uint32_t, le32)
WRITE_INTTYPE_ENCODER(int32_t, le32)
WRITE_INTTYPE_ENCODER(uint16_t, le16)
WRITE_INTTYPE_ENCODER(int16_t, le16)

#ifdef ENCODE_DUMP
# include <stdio.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>

# define ENCODE_STR(x) #x
# define ENCODE_STRINGIFY(x) ENCODE_STR(x)

# define ENCODE_DUMP_PRE()          \
  unsigned pre_off = bl.length()
# define ENCODE_DUMP_POST(cl)                       \
  do {                                  \
    static int i = 0;                           \
    i++;                                \
    int bits = 0;                           \
    for (unsigned t = i; t; bits++)                 \
      t &= t - 1;                           \
    if (bits > 2)                           \
      break;                                \
    char fn[200];                           \
    snprintf(fn, sizeof(fn), ENCODE_STRINGIFY(ENCODE_DUMP) "/%s__%d.%x", #cl, getpid(), i++); \
    int fd = ::open(fn, O_WRONLY|O_TRUNC|O_CREAT, 0644);        \
    if (fd >= 0) {                          \
      bufferlist sub;                           \
      sub.substr_of(bl, pre_off, bl.length() - pre_off);        \
      sub.write_fd(fd);                         \
      ::close(fd);                          \
    }                                   \
  } while (0)
#else
# define ENCODE_DUMP_PRE()
# define ENCODE_DUMP_POST(cl)
#endif

#define WRITE_CLASS_ENCODER(cl)                     \
  inline void encode(const cl &c, bufferlist &bl, uint64_t features=0) { \
    ENCODE_DUMP_PRE(); c.encode(bl); ENCODE_DUMP_POST(cl); }        \
  inline void decode(cl &c, bufferlist::iterator &p) { c.decode(p); }

#define WRITE_CLASS_MEMBER_ENCODER(cl)                  \
  inline void encode(const cl &c, bufferlist &bl) const {       \
    ENCODE_DUMP_PRE(); c.encode(bl); ENCODE_DUMP_POST(cl); }        \
  inline void decode(cl &c, bufferlist::iterator &p) { c.decode(p); }

#define WRITE_CLASS_ENCODER_FEATURES(cl)                \
  inline void encode(const cl &c, bufferlist &bl, uint64_t features) {  \
    ENCODE_DUMP_PRE(); c.encode(bl, features); ENCODE_DUMP_POST(cl); }  \
  inline void decode(cl &c, bufferlist::iterator &p) { c.decode(p); }

// string
inline void encode(const std::string& s, bufferlist& bl, uint64_t features=0)
{
    uint32_t len = s.length();
    encode(len, bl);
    bl.append(s.data(), len);
}

inline void decode(std::string& s, bufferlist::iterator& p)
{
    uint32_t len;
    decode(len, p);
    s.clear();
    p.copy(len, s);
}

inline void encode_nohead(const std::string& s, bufferlist& bl)
{
    bl.append(s.data(), s.length());
}
inline void decode_nohead(int len, std::string& s, bufferlist::iterator& p)
{
    s.clear();
    p.copy(len, s);
}

// const char* (encode only, string compatible)
inline void encode(const char *s, bufferlist& bl)
{
    uint32_t len = strlen(s);
    encode(len, bl);
    bl.append(s, len);
}

template<class A>
inline void encode_array_nohead(const A a[], int n, bufferlist &bl)
{
    for (int i=0; i<n; i++)
        encode(a[i], bl);
}
template<class A>
inline void decode_array_nohead(A a[], int n, bufferlist::iterator &p)
{
    for (int i=0; i<n; i++)
        decode(a[i], p);
}

// -----------------------------
// buffers

// bufferptr (encapsulated)
inline void encode(const buffer::ptr& bp, bufferlist& bl)
{
    uint32_t len = bp.length();
    encode(len, bl);
    if (len)
        bl.append(bp);
}

inline void decode(buffer::ptr& bp, bufferlist::iterator& p)
{
    uint32_t len;
    decode(len, p);

    bufferlist s;
    p.copy(len, s);

    if (len) {
        if (s.buffers().size() == 1)
            bp = s.buffers().front();
        else
            bp = buffer::copy(s.c_str(), s.length());
    }
}

// bufferlist (encapsulated)
inline void encode(const bufferlist& s, bufferlist& bl)
{
    uint32_t len = s.length();
    encode(len, bl);
    bl.append(s);
}
inline void encode_destructively(bufferlist& s, bufferlist& bl)
{
    uint32_t len = s.length();
    encode(len, bl);
    bl.claim_append(s);
}
inline void decode(bufferlist& s, bufferlist::iterator& p)
{
    uint32_t len;
    decode(len, p);
    s.clear();
    p.copy(len, s);
}

inline void encode_nohead(const bufferlist& s, bufferlist& bl)
{
    bl.append(s);
}
inline void decode_nohead(int len, bufferlist& s, bufferlist::iterator& p)
{
    s.clear();
    p.copy(len, s);
}

// full bl decoder
template<class T>
inline void decode(T &o, bufferlist& bl)
{
    bufferlist::iterator p = bl.begin();
    decode(o, p);
    assert(p.end());
}

// -----------------------------
// STL container types

// pair
template<class A, class B>
inline void encode(const std::pair<A,B> &p, bufferlist &bl, uint64_t features)
{
    encode(p.first, bl, features);
    encode(p.second, bl, features);
}
template<class A, class B>
inline void encode(const std::pair<A,B> &p, bufferlist &bl)
{
    encode(p.first, bl);
    encode(p.second, bl);
}
template<class A, class B>
inline void decode(std::pair<A,B> &pa, bufferlist::iterator &p)
{
    decode(pa.first, p);
    decode(pa.second, p);
}

// list
template<class T>
inline void encode(const std::list<T>& ls, bufferlist& bl)
{
    // should i pre- or post- count?
    if (!ls.empty()) {
        unsigned pos = bl.length();
        unsigned n = 0;
        encode(n, bl);
        for (typename std::list<T>::const_iterator p = ls.begin(); p != ls.end(); ++p) {
            n++;
            encode(*p, bl);
        }
        msgr_le32 en;
        en = n;
        bl.copy_in(pos, sizeof(en), (char*)&en);
    } else {
        uint32_t n = (uint32_t)(ls.size());    // FIXME: this is slow on a list.
        encode(n, bl);
        for (typename std::list<T>::const_iterator p = ls.begin(); p != ls.end(); ++p)
            encode(*p, bl);
    }
}
template<class T>
inline void decode(std::list<T>& ls, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    ls.clear();
    while (n--) {
        T v;
        decode(v, p);
        ls.push_back(v);
    }
}

template<class T>
inline void encode(const std::list<std::shared_ptr<T> >& ls, bufferlist& bl)
{
    // should i pre- or post- count?
    if (!ls.empty()) {
        unsigned pos = bl.length();
        unsigned n = 0;
        encode(n, bl);
        for (typename std::list<std::shared_ptr<T> >::const_iterator p = ls.begin(); p != ls.end(); ++p) {
            n++;
            encode(**p, bl);
        }
        msgr_le32 en;
        en = n;
        bl.copy_in(pos, sizeof(en), (char*)&en);
    } else {
        uint32_t n = (uint32_t)(ls.size());    // FIXME: this is slow on a list.
        encode(n, bl);
        for (typename std::list<std::shared_ptr<T> >::const_iterator p = ls.begin(); p != ls.end(); ++p)
            encode(**p, bl);
    }
}
template<class T>
inline void decode(std::list<std::shared_ptr<T> >& ls, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    ls.clear();
    while (n--) {
        std::shared_ptr<T> v(new T);
        decode(*v, p);
        ls.push_back(v);
    }
}

// set
template<class T>
inline void encode(const std::set<T>& s, bufferlist& bl)
{
    uint32_t n = (uint32_t)(s.size());
    encode(n, bl);
    for (typename std::set<T>::const_iterator p = s.begin(); p != s.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode(std::set<T>& s, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    s.clear();
    while (n--) {
        T v;
        decode(v, p);
        s.insert(v);
    }
}

// multiset
template<class T>
inline void encode(const std::multiset<T>& s, bufferlist& bl)
{
    uint32_t n = (uint32_t)(s.size());
    encode(n, bl);
    for (typename std::multiset<T>::const_iterator p = s.begin(); p != s.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode(std::multiset<T>& s, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    s.clear();
    while (n--) {
        T v;
        decode(v, p);
        s.insert(v);
    }
}

// vector (pointers)
/*template<class T>
inline void encode(const std::vector<T*>& v, bufferlist& bl)
{
    uint32_t n = v.size();
    encode(n, bl);
    for (typename std::vector<T*>::const_iterator p = v.begin(); p != v.end(); ++p)
        encode(**p, bl);
}
template<class T>
inline void decode(std::vector<T*>& v, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    v.resize(n);
    for (uint32_t i=0; i<n; i++)
        v[i] = new T(p);
}
*/

// vector
template<class T>
inline void encode(const std::vector<T>& v, bufferlist& bl, uint64_t features)
{
    uint32_t n = (uint32_t)(v.size());
    encode(n, bl);
    for (typename std::vector<T>::const_iterator p = v.begin(); p != v.end(); ++p)
        encode(*p, bl, features);
}
template<class T>
inline void encode(const std::vector<T>& v, bufferlist& bl)
{
    uint32_t n = (uint32_t)(v.size());
    encode(n, bl);
    for (typename std::vector<T>::const_iterator p = v.begin(); p != v.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode(std::vector<T>& v, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    v.resize(n);
    for (uint32_t i=0; i<n; i++)
        decode(v[i], p);
}

template<class T>
inline void encode_nohead(const std::vector<T>& v, bufferlist& bl)
{
    for (typename std::vector<T>::const_iterator p = v.begin(); p != v.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode_nohead(int len, std::vector<T>& v, bufferlist::iterator& p)
{
    v.resize(len);
    for (uint32_t i=0; i<v.size(); i++)
        decode(v[i], p);
}

// vector (shared_ptr)
template<class T>
inline void encode(const std::vector<std::shared_ptr<T> >& v, bufferlist& bl)
{
    uint32_t n = (uint32_t)(v.size());
    encode(n, bl);
    for (typename std::vector<std::shared_ptr<T> >::const_iterator p = v.begin(); p != v.end(); ++p)
        if (*p)
            encode(**p, bl);
        else
            encode(T(), bl);
}
template<class T>
inline void decode(std::vector<std::shared_ptr<T> >& v, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    v.resize(n);
    for (uint32_t i=0; i<n; i++) {
        v[i].reset(new T());
        decode(*v[i], p);
    }
}

// map (pointers)
/*
template<class T, class U>
inline void encode(const std::map<T,U*>& m, bufferlist& bl)
{
    uint32_t n = m.size();
    encode(n, bl);
    for (typename std::map<T,U*>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl);
        encode(*p->second, bl);
    }
}
template<class T, class U>
inline void decode(std::map<T,U*>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    m.clear();
    while (n--) {
        T k;
        decode(k, p);
        m[k] = new U(p);
  }
}*/


// map
template<class T, class U>
inline void encode(const std::map<T,U>& m, bufferlist& bl)
{
    uint32_t n = (uint32_t)(m.size());
    encode(n, bl);
    for (typename std::map<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl);
        encode(p->second, bl);
    }
}
template<class T, class U>
inline void encode(const std::map<T,U>& m, bufferlist& bl, uint64_t features)
{
    uint32_t n = (uint32_t)(m.size());
    encode(n, bl);
    for (typename std::map<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl, features);
        encode(p->second, bl, features);
    }
}
template<class T, class U>
inline void decode(std::map<T,U>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    m.clear();
    while (n--) {
        T k;
        decode(k, p);
        decode(m[k], p);
    }
}
template<class T, class U>
inline void decode_noclear(std::map<T,U>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    while (n--) {
        T k;
        decode(k, p);
        decode(m[k], p);
    }
}
template<class T, class U>
inline void encode_nohead(const std::map<T,U>& m, bufferlist& bl)
{
    for (typename std::map<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl);
        encode(p->second, bl);
    }
}
template<class T, class U>
inline void encode_nohead(const std::map<T,U>& m, bufferlist& bl, uint64_t features)
{
    for (typename std::map<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl, features);
        encode(p->second, bl, features);
    }
}
template<class T, class U>
inline void decode_nohead(int n, std::map<T,U>& m, bufferlist::iterator& p)
{
    m.clear();
    while (n--) {
        T k;
        decode(k, p);
        decode(m[k], p);
    }
}

// multimap
template<class T, class U>
inline void encode(const std::multimap<T,U>& m, bufferlist& bl)
{
    uint32_t n = (uint32_t)(m.size());
    encode(n, bl);
    for (typename std::multimap<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl);
        encode(p->second, bl);
    }
}
template<class T, class U>
inline void decode(std::multimap<T,U>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    m.clear();
    while (n--) {
        typename std::pair<T,U> tu = std::pair<T,U>();
        decode(tu.first, p);
        typename std::multimap<T,U>::iterator it = m.insert(tu);
        decode(it->second, p);
    }
}

// unordered_map
template<class T, class U>
inline void encode(const std::unordered_map<T,U>& m, bufferlist& bl)
{
    uint32_t n = (uint32_t)(m.size());
    encode(n, bl);
    for (typename std::unordered_map<T,U>::const_iterator p = m.begin(); p != m.end(); ++p) {
        encode(p->first, bl);
        encode(p->second, bl);
    }
}
template<class T, class U>
inline void decode(std::unordered_map<T,U>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    m.clear();
    while (n--) {
        T k;
        decode(k, p);
        decode(m[k], p);
    }
}

// unordered_set
template<class T>
inline void encode(const std::unordered_set<T>& m, bufferlist& bl)
{
    uint32_t n = (uint32_t)(m.size());
    encode(n, bl);
    for (typename std::unordered_set<T>::const_iterator p = m.begin(); p != m.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode(std::unordered_set<T>& m, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    m.clear();
    while (n--) {
        T k;
        decode(k, p);
        m.insert(k);
    }
}

// deque
template<class T>
inline void encode(const std::deque<T>& ls, bufferlist& bl)
{
    uint32_t n = ls.size();
    encode(n, bl);
    for (typename std::deque<T>::const_iterator p = ls.begin(); p != ls.end(); ++p)
        encode(*p, bl);
}
template<class T>
inline void decode(std::deque<T>& ls, bufferlist::iterator& p)
{
    uint32_t n;
    decode(n, p);
    ls.clear();
    while (n--) {
        T v;
        decode(v, p);
        ls.push_back(v);
    }
}

#define ENCODE_START(v, compat, bl)                         \
  uint8_t struct_v = v, struct_compat = compat;             \
  msgr::encode(struct_v, (bl));                             \
  msgr::encode(struct_compat, (bl));                        \
  buffer::list::iterator struct_compat_it = (bl).end();     \
  struct_compat_it.advance(-1);                             \
  msgr_le32 struct_len;                                     \
  struct_len = 0;                                           \
  msgr::encode(struct_len, (bl));                           \
  buffer::list::iterator struct_len_it = (bl).end();        \
  struct_len_it.advance(-4);                                \
  do {


#define ENCODE_FINISH_NEW_COMPAT(bl, new_struct_compat)         \
  } while (false);                                              \
  struct_len = (bl).length() - struct_len_it.get_off() - sizeof(struct_len); \
  struct_len_it.copy_in(4, (char *)&struct_len);                \
  if (new_struct_compat) {                                      \
    struct_compat = new_struct_compat;                          \
    struct_compat_it.copy_in(1, (char *)&struct_compat);        \
  }

#define ENCODE_FINISH(bl) ENCODE_FINISH_NEW_COMPAT(bl, 0)


#define DECODE_ERR_VERSION(func, v)         \
  "" #func " unknown encoding version > " #v

#define DECODE_ERR_OLDVERSION(func, v)          \
  "" #func " no longer understand old encoding version < " #v

#define DECODE_ERR_PAST(func) \
  "" #func " decode past end of struct encoding"

#define DECODE_OLDEST(oldestv)                      \
  if (struct_v < oldestv)                       \
    throw buffer::malformed_input(DECODE_ERR_OLDVERSION(__PRETTY_FUNCTION__, v));


/**
 * start a decoding block
 *
 * @param v current version of the encoding that the code supports/encodes
 * @param bl bufferlist::iterator for the encoded data
 */
#define DECODE_START(v, bl)                     \
  uint8_t struct_v, struct_compat;                      \
  msgr::decode(struct_v, bl);                           \
  msgr::decode(struct_compat, bl);                      \
  if (v < struct_compat)                                \
    throw buffer::malformed_input(DECODE_ERR_VERSION(__PRETTY_FUNCTION__, v)); \
  uint32_t struct_len;                                  \
  msgr::decode(struct_len, bl);                         \
  if (struct_len > bl.get_remaining())                  \
    throw buffer::malformed_input(DECODE_ERR_PAST(__PRETTY_FUNCTION__)); \
  unsigned struct_end = bl.get_off() + struct_len;      \
  do {


#define __DECODE_START_LEGACY_COMPAT_LEN(v, compatv, lenv, skip_v, bl)  \
  uint8_t struct_v;                                 \
  msgr::decode(struct_v, bl);                       \
  if (struct_v >= compatv) {                        \
    uint8_t struct_compat;                          \
    msgr::decode(struct_compat, bl);                \
    if (v < struct_compat)                          \
      throw buffer::malformed_input(DECODE_ERR_VERSION(__PRETTY_FUNCTION__, v)); \
  } else if (skip_v) {                              \
    if ((int)bl.get_remaining() < skip_v)           \
      throw buffer::malformed_input(DECODE_ERR_PAST(__PRETTY_FUNCTION__)); \
    bl.advance(skip_v);                             \
  }                                                 \
  unsigned struct_end = 0;                          \
  if (struct_v >= lenv) {                           \
    uint32_t struct_len;                            \
    msgr::decode(struct_len, bl);                   \
    if (struct_len > bl.get_remaining())            \
      throw buffer::malformed_input(DECODE_ERR_PAST(__PRETTY_FUNCTION__)); \
    struct_end = bl.get_off() + struct_len;         \
  }                                                 \
  do {

/**
 * start a decoding block with legacy support for older encoding schemes
 *
 * The old encoding schemes has a uint8_t struct_v only, or lacked either
 * the compat version or length.  Skip those fields conditionally.
 *
 * Most of the time, v, compatv, and lenv will all match the version
 * where the structure was switched over to the new macros.
 *
 * @param v current version of the encoding that the code supports/encodes
 * @param compatv oldest version that includes a uint8_t compat version field
 * @param lenv oldest version that includes a uint32_t length wrapper
 * @param bl bufferlist::iterator containing the encoded data
 */
#define DECODE_START_LEGACY_COMPAT_LEN(v, compatv, lenv, bl)        \
  __DECODE_START_LEGACY_COMPAT_LEN(v, compatv, lenv, 0, bl)

/**
 * start a decoding block with legacy support for older encoding schemes
 *
 * This version of the macro assumes the legacy encoding had a 32 bit
 * version
 *
 * The old encoding schemes has a uint8_t struct_v only, or lacked either
 * the compat version or length.  Skip those fields conditionally.
 *
 * Most of the time, v, compatv, and lenv will all match the version
 * where the structure was switched over to the new macros.
 *
 * @param v current version of the encoding that the code supports/encodes
 * @param compatv oldest version that includes a uint8_t compat version field
 * @param lenv oldest version that includes a uint32_t length wrapper
 * @param bl bufferlist::iterator containing the encoded data
 */
#define DECODE_START_LEGACY_COMPAT_LEN_32(v, compatv, lenv, bl)     \
  __DECODE_START_LEGACY_COMPAT_LEN(v, compatv, lenv, 3, bl)

#define DECODE_START_LEGACY_COMPAT_LEN_16(v, compatv, lenv, bl)     \
  __DECODE_START_LEGACY_COMPAT_LEN(v, compatv, lenv, 1, bl)

/**
 * finish decode block
 *
 * @param bl bufferlist::iterator we were decoding from
 */
#define DECODE_FINISH(bl)                               \
  } while (false);                                      \
  if (struct_end) {                                     \
    if (bl.get_off() > struct_end)                      \
      throw buffer::malformed_input(DECODE_ERR_PAST(__PRETTY_FUNCTION__)); \
    if (bl.get_off() < struct_end)                      \
      bl.advance(struct_end - bl.get_off());            \
  }

} // namespace msgr

#endif // ENCODING_9331533892514b4cbc0268ae6e4dbbca_H
