#include "crc32c_intel_fast.h"
#include "crc32c_intel_baseline.h"
#include "acconfig.h"

extern unsigned int crc32_iscsi_00(unsigned char const *buffer, int len,
    unsigned int crc);
extern unsigned int crc32_iscsi_zero_00(unsigned char const *buffer, int len,
    unsigned int crc);

#ifdef MSGR_HAVE_GOOD_YASM_ELF64
uint32_t msgr_crc32c_intel_fast(uint32_t crc, unsigned char const *buffer,
    unsigned len)
{
	uint32_t v;
	unsigned left;

	if (!buffer)
		return crc32_iscsi_zero_00(buffer, len, crc);

	/*
	 * the crc32_iscsi_00 method reads past buffer+len (because it
	 * reads full words) which makes valgrind unhappy.  don't do
	 * that.
	 */
	if (len < 16)
		return msgr_crc32c_intel_baseline(crc, buffer, len);
	left = ((unsigned long)buffer + len) & 7;
	len -= left;
	v = crc32_iscsi_00(buffer, len, crc);
	if (left)
		v = msgr_crc32c_intel_baseline(v, buffer + len, left);
	return v;
}

int msgr_crc32c_intel_fast_exists(void)
{
	return 1;
}

#else

int msgr_crc32c_intel_fast_exists(void)
{
	return 0;
}

uint32_t msgr_crc32c_intel_fast(uint32_t crc, unsigned char const *buffer,
    unsigned len)
{
	return 0;
}

#endif
