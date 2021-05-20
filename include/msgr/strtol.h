#ifndef STRTOL_aae935ae33f846a2a0180bdb361e8138_H
#define STRTOL_aae935ae33f846a2a0180bdb361e8138_H

#include <string>
#include <limits>
extern "C" {
#include <stdint.h>
}

long long strict_strtoll(const char *str, int base, std::string *err);

int strict_strtol(const char *str, int base, std::string *err);

double strict_strtod(const char *str, std::string *err);

float strict_strtof(const char *str, std::string *err);

uint64_t strict_sistrtoll(const char *str, std::string *err);

template <typename Target>
Target strict_si_cast(const char *str, std::string *err) {
  uint64_t ret = strict_sistrtoll(str, err);
  if (!err->empty())
    return ret;
  if (ret > (uint64_t)std::numeric_limits<Target>::max()) {
    err->append("The option value '");
    err->append(str);
    err->append("' seems to be too large");
    return 0;
  }
  return ret;
}

template <>
uint64_t strict_si_cast(const char *str, std::string *err);

#endif
