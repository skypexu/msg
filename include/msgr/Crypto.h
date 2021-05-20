#ifndef CRYPTO_0e654b23a192_H
#define CRYPTO_0e654b23a192_H

#include "types.h"
#include "utime.h"

#include "buffer.h"

#include <string>

namespace msgr {

class MsgrContext;
class CryptoHandler;

/*
 * match encoding of struct ceph_secret
 */
class CryptoKey {
protected:
  uint16_t type;
  utime_t created;
  bufferptr secret;

  // cache a pointer to the handler, so we don't have to look it up
  // for each crypto operation
  mutable CryptoHandler *ch;

public:
  CryptoKey() : type(0), ch(NULL) { }
  CryptoKey(int t, utime_t c, bufferptr& s) : type(t), created(c), secret(s), ch(NULL) { }

  void encode(bufferlist& bl) const {
    msgr::encode(type, bl);
    msgr::encode(created, bl);
    uint16_t len = secret.length();
    msgr::encode(len, bl);
    bl.append(secret);
  }
  void decode(bufferlist::iterator& bl) {
    msgr::decode(type, bl);
    msgr::decode(created, bl);
    uint16_t len;
    msgr::decode(len, bl);
    bl.copy(len, secret);
    secret.c_str();   // make sure it's a single buffer!
  }

  int get_type() const { return type; }
  utime_t get_created() const { return created; }
  void print(std::ostream& out) const;

  int set_secret(MsgrContext *pct, int type, bufferptr& s);
  bufferptr& get_secret() { return secret; }
  const bufferptr& get_secret() const { return secret; }

  void encode_base64(std::string& s) const {
    bufferlist bl;
    encode(bl);
    bufferlist e;
    bl.encode_base64(e);
    e.append('\0');
    s = e.c_str();
  }
  std::string encode_base64() const {
    std::string s;
    encode_base64(s);
    return s;
  }
  void decode_base64(const std::string& s) {
    bufferlist e;
    e.append(s);
    bufferlist bl;
    bl.decode_base64(e);
    bufferlist::iterator p = bl.begin();
    decode(p);
  }

  void encode_plaintext(bufferlist &bl);

  // --
  int create(MsgrContext *cct, int type);
  void encrypt(MsgrContext *cct, const bufferlist& in, bufferlist& out, std::string &error) const;
  void decrypt(MsgrContext *cct, const bufferlist& in, bufferlist& out, std::string &error) const;

  void to_str(std::string& s) const;
};
WRITE_CLASS_ENCODER(CryptoKey)

static inline std::ostream& operator<<(std::ostream& out, const CryptoKey& k)
{
  k.print(out);
  return out;
}


/*
 * Driver for a particular algorithm
 *
 * To use these functions, you need to call ceph::crypto::init(), see
 * common/ceph_crypto.h. common_init_finish does this for you.
 */
class CryptoHandler {
public:
  virtual ~CryptoHandler() {}
  virtual int get_type() const = 0;
  virtual int create(bufferptr& secret) = 0;
  virtual int validate_secret(bufferptr& secret) = 0;
  virtual void encrypt(const bufferptr& secret, const bufferlist& in,
		      bufferlist& out, std::string &error) const = 0;
  virtual void decrypt(const bufferptr& secret, const bufferlist& in,
		      bufferlist& out, std::string &error) const = 0;
};

extern int get_random_bytes(char *buf, int len);
extern uint64_t get_random(uint64_t min_val, uint64_t max_val);

class CryptoNone : public CryptoHandler {
public:
  CryptoNone() { }
  ~CryptoNone() {}
  int get_type() const {
    return MSGR_CRYPTO_NONE;
  }
  int create(bufferptr& secret);
  int validate_secret(bufferptr& secret);
  void encrypt(const bufferptr& secret, const bufferlist& in,
	      bufferlist& out, std::string &error) const;
  void decrypt(const bufferptr& secret, const bufferlist& in,
	      bufferlist& out, std::string &error) const;
};

class CryptoAES : public CryptoHandler {
public:
  CryptoAES() { }
  ~CryptoAES() {}
  int get_type() const {
    return MSGR_CRYPTO_AES;
  }
  int create(bufferptr& secret);
  int validate_secret(bufferptr& secret);
  void encrypt(const bufferptr& secret, const bufferlist& in,
	       bufferlist& out, std::string &error) const;
  void decrypt(const bufferptr& secret, const bufferlist& in, 
	      bufferlist& out, std::string &error) const;
};

} // namespace msgr
#endif // CRYPTO_0e654b23a192_H
