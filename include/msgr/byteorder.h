#ifndef BYTEORDER_60d5d54091994dca8676ad7c3519430b_H
#define BYTEORDER_60d5d54091994dca8676ad7c3519430b_H

#include <stdint.h>
#include <endian.h>

namespace msgr {

static __inline__ uint16_t swab16(uint16_t val)
{
    return (val >> 8) | (val << 8);
}

static __inline__ uint32_t swab32(uint32_t val)
{
    return ((val >> 24) |
           ((val >> 8)  & 0xff00) |
           ((val << 8)  & 0xff0000) |
           ((val << 24)));
}

static __inline__ uint64_t swab64(uint64_t val)
{
    return ((val >> 56) |
           ((val >> 40) & 0xff00ull) |
           ((val >> 24) & 0xff0000ull) |
           ((val >> 8)  & 0xff000000ull) |
           ((val << 8)  & 0xff00000000ull) |
           ((val << 24) & 0xff0000000000ull) |
           ((val << 40) & 0xff000000000000ull) |
           ((val << 56)));
}

// mswab == maybe swab (if not LE)
#if  __BYTE_ORDER == __BIG_ENDIAN
#define mswab64(a) swab64(a)
#define mswab32(a) swab32(a)
# define mswab16(a) swab16(a)
#elif __BYTE_ORDER == __LITTLE_ENDIAN
# define mswab64(a) (a)
# define mswab32(a) (a)
# define mswab16(a) (a)
#else
# error "Could not determine endianess"
#endif


#ifdef __cplusplus

#define MAKE_LE_CLASS(bits)                                 \
  struct msgr_le##bits {                                    \
    uint##bits##_t v;                                       \
    msgr_le##bits &operator=(uint##bits##_t nv) {           \
        v = mswab##bits(nv);                                \
        return *this;                                       \
    }                                                       \
    operator uint##bits##_t() const { return mswab##bits(v); }          \
  } __attribute__ ((packed));                                           \
  static inline bool operator==(msgr_le##bits a, msgr_le##bits b) {     \
        return a.v == b.v;                                              \
  }

MAKE_LE_CLASS(64)
MAKE_LE_CLASS(32)
MAKE_LE_CLASS(16)

typedef struct msgr_le16  __le16;
typedef struct msgr_le32  __le32;
typedef struct msgr_le64  __le64;

#undef MAKE_LE_CLASS

#endif /* __cplusplus */

#define init_le64(x) { (uint64_t)mswab64(x) }
#define init_le32(x) { (uint32_t)mswab32(x) }
#define init_le16(x) { (uint16_t)mswab16(x) }

/*
 *#define cpu_to_le64(x) (x)
 *#define cpu_to_le32(x) (x)
 *#define cpu_to_le16(x) (x)
 */
#define le64_to_cpu(x) ((uint64_t)x)
#define le32_to_cpu(x) ((uint32_t)x)
#define le16_to_cpu(x) ((uint16_t)x)

typedef struct msgr_le16  __le16;
typedef struct msgr_le32  __le32;
typedef struct msgr_le64  __le64;

} //namespace msgr

#endif // BYTEORDER_60d5d54091994dca8676ad7c3519430b_H
