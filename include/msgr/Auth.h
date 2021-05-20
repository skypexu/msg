#ifndef AUTHAUTHORIZER_601cc59be75e_H
#define AUTHAUTHORIZER_601cc59be75e_H

#include "Crypto.h"

namespace msgr {

/*                                                                              
 * abstract authorizer class                                                    
 */                                                                             
struct AuthAuthorizer {                                                         
  uint32_t protocol;
  bufferlist bl;
  CryptoKey session_key;
                                                                                
  AuthAuthorizer(uint32_t p) : protocol(p) {}
  virtual ~AuthAuthorizer() {}
  virtual bool verify_reply(bufferlist::iterator& reply) = 0;
};

class AuthSessionHandler;
}

#endif
