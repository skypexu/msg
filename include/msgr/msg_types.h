#ifndef MSG_TYPES_2b4235aac3eb47648716ccf70dedac2e_H
#define MSG_TYPES_2b4235aac3eb47648716ccf70dedac2e_H

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "types.h"
#include "hash.h"
#include "blobhash.h"
#include "hash_namespace.h"


namespace msgr {

class entity_name_t {
public:
  uint8_t _type;
  int64_t _num;

public:
  static const int TYPE_MON = MSGR_ENTITY_TYPE_MON;
  static const int TYPE_OSD = MSGR_ENTITY_TYPE_OSD;
  static const int TYPE_CLIENT = MSGR_ENTITY_TYPE_CLIENT;

  static const int NEW = -1;

  // cons
  entity_name_t() : _type(0), _num(0) { }
  entity_name_t(int t, int64_t n) : _type(t), _num(n) { }
  entity_name_t(const msgr_entity_name &n) :
    _type(n.type), _num(n.num) { }

  // static cons
  static entity_name_t MON(int i=NEW) { return entity_name_t(TYPE_MON, i); }
  static entity_name_t OSD(int i=NEW) { return entity_name_t(TYPE_OSD, i); }
  static entity_name_t CLIENT(int i=NEW) { return entity_name_t(TYPE_CLIENT, i); }

  int64_t num() const { return _num; }
  int type() const { return _type; }
  const char *type_str() const {
    return msgr_entity_type_name(type());
  }

  bool is_new() const { return num() < 0; }

  bool is_client() const { return type() == TYPE_CLIENT; }
  bool is_osd() const { return type() == TYPE_OSD; }
  bool is_mon() const { return type() == TYPE_MON; }

  operator msgr_entity_name() const {
    msgr_entity_name n = { _type, init_le64(_num) };
    return n;
  }

  bool parse(const std::string& s) {
    const char *start = s.c_str();
    char *end;
    bool got = parse(start, &end);
    return got && end == start + s.length();
  }
  bool parse(const char *start, char **end) {
    if (strstr(start, "mon.") == start) {
      _type = TYPE_MON;
      start += 4;
    } else if (strstr(start, "osd.") == start) {
      _type = TYPE_OSD;
      start += 5;
    } else if (strstr(start, "client.") == start) {
      _type = TYPE_CLIENT;
      start += 7;
    } else {
      return false;
    }
    if (isspace(*start))
      return false;
    _num = strtoll(start, end, 10);
    if (*end == NULL || *end == start)
      return false;
    return true;
  }

  void encode(bufferlist& bl) const {
    msgr::encode(_type, bl);
    msgr::encode(_num, bl);
  }
  void decode(bufferlist::iterator& bl) {
    msgr::decode(_type, bl);
    msgr::decode(_num, bl);
  }

};
WRITE_CLASS_ENCODER(entity_name_t)

inline bool operator== (const entity_name_t& l, const entity_name_t& r) {
  return (l.type() == r.type()) && (l.num() == r.num()); }
inline bool operator!= (const entity_name_t& l, const entity_name_t& r) {
  return (l.type() != r.type()) || (l.num() != r.num()); }
inline bool operator< (const entity_name_t& l, const entity_name_t& r) {
  return (l.type() < r.type()) || (l.type() == r.type() && l.num() < r.num()); }



/*
 * an entity's network address.
 * includes a random value that prevents it from being reused.
 * thus identifies a particular process instance.
 * ipv4 for now.
 */

/*
 * encode sockaddr.ss_family in big endian
 */
static inline void encode(const sockaddr_storage& a, bufferlist& bl) {
  struct sockaddr_storage ss = a;
#if !defined(__FreeBSD__)
  ss.ss_family = htons(ss.ss_family);
#endif
  encode_raw(ss, bl);
}
static inline void decode(sockaddr_storage& a, bufferlist::iterator& bl) {
  msgr::decode_raw(a, bl);
#if !defined(__FreeBSD__)
  a.ss_family = ntohs(a.ss_family);
#endif
}

struct entity_addr_t {
  uint32_t type;
  uint32_t nonce;
  union {
    sockaddr_storage addr;
    sockaddr_in addr4;
    sockaddr_in6 addr6;
  };

  unsigned int addr_size() const {
    switch (addr.ss_family) {
    case AF_INET:
      return sizeof(addr4);
      break;
    case AF_INET6:
      return sizeof(addr6);
      break;
    }
    return sizeof(addr);
  }

  entity_addr_t() : type(0), nonce(0) {
    memset(&addr, 0, sizeof(addr));
  }
  entity_addr_t(const msgr_entity_addr &o) {
    type = o.type;
    nonce = o.nonce;
    addr = o.in_addr;
#if !defined(__FreeBSD__)
    addr.ss_family = ntohs(addr.ss_family);
#endif
  }

  uint32_t get_nonce() const { return nonce; }
  void set_nonce(uint32_t n) { nonce = n; }

  int get_family() const {
    return addr.ss_family;
  }
  void set_family(int f) {
    addr.ss_family = f;
  }

  sockaddr_storage &ss_addr() {
    return addr;
  }
  sockaddr_in &in4_addr() {
    return addr4;
  }
  sockaddr_in6 &in6_addr() {
    return addr6;
  }

  bool set_sockaddr(struct sockaddr *sa)
  {
    switch (sa->sa_family) {
    case AF_INET:
      memcpy(&addr4, sa, sizeof(sockaddr_in));
      break;
    case AF_INET6:
      memcpy(&addr6, sa, sizeof(sockaddr_in6));
      break;
    default:
      return false;
    }
    return true;
  }

  void set_in4_quad(int pos, int val) {
    addr4.sin_family = AF_INET;
    unsigned char *ipq = (unsigned char*)&addr4.sin_addr.s_addr;
    ipq[pos] = val;
  }
  void set_port(int port) {
    switch (addr.ss_family) {
    case AF_INET:
      addr4.sin_port = htons(port);
      break;
    case AF_INET6:
      addr6.sin6_port = htons(port);
      break;
    default:
      assert(0);
    }
  }
  int get_port() const {
    switch (addr.ss_family) {
    case AF_INET:
      return ntohs(addr4.sin_port);
      break;
    case AF_INET6:
      return ntohs(addr6.sin6_port);
      break;
    }
    return 0;
  }

  operator msgr_entity_addr() const {
    msgr_entity_addr a;
    a.type = 0;
    a.nonce = nonce;
    a.in_addr = addr;
#if !defined(__FreeBSD__)
    a.in_addr.ss_family = htons(addr.ss_family);
#endif
    return a;
  }

  bool probably_equals(const entity_addr_t &o) const {
    if (get_port() != o.get_port())
      return false;
    if (get_nonce() != o.get_nonce())
      return false;
    if (is_blank_ip() || o.is_blank_ip())
      return true;
    if (memcmp(&addr, &o.addr, sizeof(addr)) == 0)
      return true;
    return false;
  }

  bool is_same_host(const entity_addr_t &o) const {
    if (addr.ss_family != o.addr.ss_family)
      return false;
    if (addr.ss_family == AF_INET)
      return addr4.sin_addr.s_addr == o.addr4.sin_addr.s_addr;
    if (addr.ss_family == AF_INET6)
      return memcmp(addr6.sin6_addr.s6_addr,
		    o.addr6.sin6_addr.s6_addr,
		    sizeof(addr6.sin6_addr.s6_addr)) == 0;
    return false;
  }

  bool is_blank_ip() const {
    switch (addr.ss_family) {
    case AF_INET:
      return addr4.sin_addr.s_addr == INADDR_ANY;
    case AF_INET6:
      return memcmp(&addr6.sin6_addr, &in6addr_any, sizeof(in6addr_any)) == 0;
    default:
      return true;
    }
  }

  bool is_ip() const {
    switch (addr.ss_family) {
    case AF_INET:
    case AF_INET6:
      return true;
    default:
      return false;
    }
  }

  bool parse(const char *s, const char **end = 0);

  void encode(bufferlist& bl) const {
    msgr::encode(type, bl);
    msgr::encode(nonce, bl);
    msgr::encode(addr, bl);
  }
  void decode(bufferlist::iterator& bl) {
    msgr::decode(type, bl);
    msgr::decode(nonce, bl);
    msgr::decode(addr, bl);
  }
};
WRITE_CLASS_ENCODER(entity_addr_t)

inline bool operator==(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) == 0; }
inline bool operator!=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) != 0; }
inline bool operator<(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) < 0; }
inline bool operator<=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) <= 0; }
inline bool operator>(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) > 0; }
inline bool operator>=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) >= 0; }

/*
 * a particular entity instance
 */
struct entity_inst_t {
  entity_name_t name;
  entity_addr_t addr;
  entity_inst_t() {}
  entity_inst_t(entity_name_t n, const entity_addr_t& a) : name(n), addr(a) {}
  entity_inst_t(const msgr_entity_inst& i) : name(i.name), addr(i.addr) { }
  entity_inst_t(const msgr_entity_name& n, const msgr_entity_addr &a) : name(n), addr(a) {}
  operator msgr_entity_inst() {
    msgr_entity_inst i = {name, addr};
    return i;
  }

  void encode(bufferlist& bl) const {
    msgr::encode(name, bl);
    msgr::encode(addr, bl);
  }
  void decode(bufferlist::iterator& bl) {
    msgr::decode(name, bl);
    msgr::decode(addr, bl);
  }
};
WRITE_CLASS_ENCODER(entity_inst_t)


inline bool operator==(const entity_inst_t& a, const entity_inst_t& b) {
  return a.name == b.name && a.addr == b.addr;
}
inline bool operator!=(const entity_inst_t& a, const entity_inst_t& b) {
  return a.name != b.name || a.addr != b.addr;
}
inline bool operator<(const entity_inst_t& a, const entity_inst_t& b) {
  return a.name < b.name || (a.name == b.name && a.addr < b.addr);
}
inline bool operator<=(const entity_inst_t& a, const entity_inst_t& b) {
  return a.name < b.name || (a.name == b.name && a.addr <= b.addr);
}
inline bool operator>(const entity_inst_t& a, const entity_inst_t& b) { return b < a; }
inline bool operator>=(const entity_inst_t& a, const entity_inst_t& b) { return b <= a; }

} // namespace msgr

namespace std {

ostream& operator<<(ostream& out, const ::sockaddr_storage &ss);

inline ostream& operator<<(ostream& out, const msgr::entity_addr_t &addr)
{
  return out << addr.addr << '/' << addr.nonce;
}

inline ostream& operator<<(ostream& out, const msgr::entity_name_t& addr) {
  //if (addr.is_namer()) return out << "namer";
  if (addr.is_new() || addr.num() < 0)
    return out << addr.type_str() << ".?";
  else
    return out << addr.type_str() << '.' << addr.num();
}

inline ostream& operator<<(ostream& out, const msgr::msgr_entity_name& addr) {
  return out << *(const msgr::entity_name_t*)&addr;
}

inline ostream& operator<<(ostream& out, const msgr::entity_inst_t &i)
{
  return out << i.name << " " << i.addr;
}

inline ostream& operator<<(ostream& out, const msgr::msgr_entity_inst &i)
{
  msgr::entity_inst_t n = i;
  return out << n;
}

}

// namespace std
MSGR_HASH_NAMESPACE_START
  template<> struct hash< msgr::entity_addr_t >
  {
    size_t operator()( const msgr::entity_addr_t& x ) const
    {
      static msgr::blobhash H;
      return H((const char*)&x, sizeof(x));
    }
  };
MSGR_HASH_NAMESPACE_END

MSGR_HASH_NAMESPACE_START
  template<> struct hash< msgr::entity_name_t >
  {
    size_t operator()( const msgr::entity_name_t &m ) const
    {
      return msgr::rjhash32(m.type() ^ m.num());
    }
  };
MSGR_HASH_NAMESPACE_END

MSGR_HASH_NAMESPACE_START
  template<> struct hash< msgr::entity_inst_t >
  {
    size_t operator()( const msgr::entity_inst_t& x ) const
    {
      static hash< msgr::entity_name_t > H;
      static hash< msgr::entity_addr_t > I;
      return H(x.name) ^ I(x.addr);
    }
  };
MSGR_HASH_NAMESPACE_END

#endif
