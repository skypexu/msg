#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "msgr/msg_types.h"

using namespace std;

namespace msgr {

bool entity_addr_t::parse(const char *s, const char **end)
{
  memset(this, 0, sizeof(*this));

  const char *start = s;
  bool brackets = false;
  if (*start == '[') {
    start++;
    brackets = true;
  }
  
  // inet_pton() requires a null terminated input, so let's fill two
  // buffers, one with ipv4 allowed characters, and one with ipv6, and
  // then see which parses.
  char buf4[39];
  char *o = buf4;
  const char *p = start;
  while (o < buf4 + sizeof(buf4) &&
	 *p && ((*p == '.') ||
		(*p >= '0' && *p <= '9'))) {
    *o++ = *p++;
  }
  *o = 0;

  char buf6[64];  // actually 39 + null is sufficient.
  o = buf6;
  p = start;
  while (o < buf6 + sizeof(buf6) &&
	 *p && ((*p == ':') ||
		(*p >= '0' && *p <= '9') ||
		(*p >= 'a' && *p <= 'f') ||
		(*p >= 'A' && *p <= 'F'))) {
    *o++ = *p++;
  }
  *o = 0;
  //cout << "buf4 is '" << buf4 << "', buf6 is '" << buf6 << "'" << std::endl;

  // ipv4?
  struct in_addr a4;
  struct in6_addr a6;
  if (inet_pton(AF_INET, buf4, &a4)) {
    addr4.sin_addr.s_addr = a4.s_addr;
    addr.ss_family = AF_INET;
    p = start + strlen(buf4);
  } else if (inet_pton(AF_INET6, buf6, &a6)) {
    addr.ss_family = AF_INET6;
    memcpy(&addr6.sin6_addr, &a6, sizeof(a6));
    p = start + strlen(buf6);
  } else {
    return false;
  }

  if (brackets) {
    if (*p != ']')
      return false;
    p++;
  }
  
  //cout << "p is " << *p << std::endl;
  if (*p == ':') {
    // parse a port, too!
    p++;
    int port = atoi(p);
    set_port(port);
    while (*p && *p >= '0' && *p <= '9')
      p++;
  }

  if (*p == '/') {
    // parse nonce, too
    p++;
    int non = atoi(p);
    set_nonce(non);
    while (*p && *p >= '0' && *p <= '9')
      p++;
  }

  if (end)
    *end = p;

  //cout << *this << std::endl;
  return true;
}

} //namespace msgr

namespace std {

ostream& operator<<(ostream& out, const ::sockaddr_storage &ss)
{
  char buf[NI_MAXHOST] = { 0 };
  char serv[NI_MAXSERV] = { 0 };
  size_t hostlen;

  if (ss.ss_family == AF_INET)
    hostlen = sizeof(struct sockaddr_in);
  else if (ss.ss_family == AF_INET6)
    hostlen = sizeof(struct sockaddr_in6);
  else {
    hostlen = sizeof(struct sockaddr_storage);
  }
  getnameinfo((struct sockaddr *)&ss, hostlen, buf, sizeof(buf),
	      serv, sizeof(serv),
	      NI_NUMERICHOST | NI_NUMERICSERV);
  if (ss.ss_family == AF_INET6)
    return out << '[' << buf << "]:" << serv;
  return out //<< ss.ss_family << ":"
	     << buf << ':' << serv;
}

} // namespace std
