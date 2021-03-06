#ifndef MSGR_CRC32C_INTEL_BASELINE_H
#define MSGR_CRC32C_INTEL_BASELINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t msgr_crc32c_intel_baseline(uint32_t crc, unsigned char const *buffer, unsigned len);

#ifdef __cplusplus
}
#endif

#endif
