#ifndef MSGR_CRC32C_INTEL_FAST_H
#define MSGR_CRC32C_INTEL_FAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* is the fast version compiled in */
extern int msgr_crc32c_intel_fast_exists(void);

#ifdef __x86_64__

extern uint32_t msgr_crc32c_intel_fast(uint32_t crc, unsigned char const *buffer, unsigned len);

#else

static inline uint32_t msgr_crc32c_intel_fast(uint32_t crc, unsigned char const *buffer, unsigned len)
{
	return 0;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
