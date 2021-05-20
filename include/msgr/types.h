#ifndef TYPES_7c113a78042b4f6988ec075bef21b927_H
#define TYPES_7c113a78042b4f6988ec075bef21b927_H

#include "encoding.h"
#include "msgr.h"
#include "msgr_timespec.h"

typedef uint64_t msgr_tid_t; // transaction id

#define MSGR_OSDC_PROTOCOL   1 /* server/client */                             
#define MSGR_MDSC_PROTOCOL   1 /* server/client */                             
#define MSGR_MONC_PROTOCOL   1 /* server/client */

/* crypto algorithms */                                                         
#define MSGR_CRYPTO_NONE 0x0                                                    
#define MSGR_CRYPTO_AES  0x1

/* security/authentication protocols */                                         
#define MSGR_AUTH_UNKNOWN   0x0                                                 
#define MSGR_AUTH_NONE      0x1                                                 
#define MSGR_AUTH_MSGRX     0x2

// -- io helpers --

namespace msgr {

template<class A, class B>
inline std::ostream& operator<<(std::ostream& out, const std::pair<A,B>& v) {
  return out << v.first << "," << v.second;
}

template<class A>
inline std::ostream& operator<<(std::ostream& out, const std::vector<A>& v) {
  out << "[";
  for (typename std::vector<A>::const_iterator p = v.begin(); p != v.end(); ++p) {
    if (p != v.begin()) out << ",";
    out << *p;
  }
  out << "]";
  return out;
}
template<class A>
inline std::ostream& operator<<(std::ostream& out, const std::deque<A>& v) {
  out << "<";
  for (typename std::deque<A>::const_iterator p = v.begin(); p != v.end(); ++p) {
    if (p != v.begin()) out << ",";
    out << *p;
  }
  out << ">";
  return out;
}

template<class A>
inline std::ostream& operator<<(std::ostream& out, const std::list<A>& ilist) {
  for (typename std::list<A>::const_iterator it = ilist.begin();
       it != ilist.end();
       ++it) {
    if (it != ilist.begin()) out << ",";
    out << *it;
  }
  return out;
}

template<class A>
inline std::ostream& operator<<(std::ostream& out, const std::set<A>& iset) {
  for (typename std::set<A>::const_iterator it = iset.begin();
       it != iset.end();
       ++it) {
    if (it != iset.begin()) out << ",";
    out << *it;
  }
  return out;
}

template<class A>
inline std::ostream& operator<<(std::ostream& out, const std::multiset<A>& iset) {
  for (typename std::multiset<A>::const_iterator it = iset.begin();
       it != iset.end();
       ++it) {
    if (it != iset.begin()) out << ",";
    out << *it;
  }
  return out;
}

template<class A,class B>
inline std::ostream& operator<<(std::ostream& out, const std::map<A,B>& m) 
{
  out << "{";
  for (typename std::map<A,B>::const_iterator it = m.begin();
       it != m.end();
       ++it) {
    if (it != m.begin()) out << ",";
    out << it->first << "=" << it->second;
  }
  out << "}";
  return out;
}

template<class A,class B>
inline std::ostream& operator<<(std::ostream& out, const std::multimap<A,B>& m) 
{
  out << "{{";
  for (typename std::multimap<A,B>::const_iterator it = m.begin();
       it != m.end();
       ++it) {
    if (it != m.begin()) out << ",";
    out << it->first << "=" << it->second;
  }
  out << "}}";
  return out;
}

template <typename T>
class output_obj_ref {
public:
  const T& obj;
  output_obj_ref(const output_obj_ref& rhs) : obj(rhs.obj) {}
  output_obj_ref(const T& o) : obj(o) {}
};

template<typename T>
output_obj_ref<T> o_ref(const T& o) {
  return output_obj_ref<T>(o);
}

template <typename T>
inline std::ostream& operator<<(std::ostream& out,
    const output_obj_ref<T>& m) {
  return out << m.obj;
}

} //namespace msgr

#endif // TYPES_7c113a78042b4f6988ec075bef21b927_H
