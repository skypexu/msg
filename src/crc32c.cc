#include "msgr/crc32c.h"

#include "probe.h"
#include "intel.h"
#include "sctp_crc32.h"
#include "crc32c_intel_baseline.h"
#include "crc32c_intel_fast.h"

/*
 * choose best implementation based on the CPU architecture.
 */
msgr_crc32c_func_t msgr_choose_crc32(void)
{
  // make sure we've probed cpu features; this might depend on the
  // link order of this file relative to probe.cc.
  msgr_arch_probe();

  // if the CPU supports it, *and* the fast version is compiled in,
  // use that.
  if (msgr_arch_intel_sse42 && msgr_crc32c_intel_fast_exists()) {
    return msgr_crc32c_intel_fast;
  }

  // default
  return msgr_crc32c_sctp;
}

/*
 * static global
 *
 * This is a bit of a no-no for shared libraries, but we don't care.
 * It is effectively constant for the executing process as the value
 * depends on the CPU architecture.
 *
 * We initialize it during program init using the magic of C++.
 */
msgr_crc32c_func_t msgr_crc32c_func = msgr_choose_crc32();

