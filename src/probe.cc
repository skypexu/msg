#include "intel.h"
#include "arm.h"
#include "probe.h"

int msgr_arch_probe(void)
{
  if (msgr_arch_probed)
    return 1;

  msgr_arch_intel_probe();
  msgr_arch_arm_probe();

  msgr_arch_probed = 1;
  return 1;
}

// do this once using the magic of c++.
int msgr_arch_probed = msgr_arch_probe();
