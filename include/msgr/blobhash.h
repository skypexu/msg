#ifndef BLOBHASH_397a3c1bfe9d4455b086fc0055da0a26_H
#define BLOBHASH_397a3c1bfe9d4455b086fc0055da0a26_H

#include "hash.h"

namespace msgr {

/*
- this is to make some of the STL types work with 64 bit values, string hash keys, etc.
- added when i was using an old STL.. maybe try taking these out and see if things 
  compile now?
*/

class blobhash {
public:
  uint32_t operator()(const char *p, unsigned len) {
    static rjhash<uint32_t> H;
    uint32_t acc = 0;
    while (len >= sizeof(acc)) {
      acc ^= *(uint32_t*)p;
      p += sizeof(uint32_t);
      len -= sizeof(uint32_t);
    }
    int sh = 0;
    while (len) {
      acc ^= (uint32_t)*p << sh;
      sh += 8;
      len--;
      p++;
    }
    return H(acc);
  }
};

} //namespace msgr

#endif // BLOBHASH_397a3c1bfe9d4455b086fc0055da0a26_H
