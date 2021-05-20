#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <algorithm>
#include <errno.h>

#include "msgr/cpp_errno.h"
#include "msgr/io_priority.h"

pid_t msgr_gettid(void)
{
#ifdef __linux__
  return syscall(SYS_gettid);
#else
  return -ENOSYS;
#endif
}

int msgr_ioprio_set(int whence, int who, int ioprio)
{
#ifdef __linux__
  return syscall(SYS_ioprio_set, whence, who, ioprio);
#else
  return -ENOSYS;
#endif
}

int msgr_ioprio_string_to_class(const std::string& s)
{
  std::string l = s;
  std::transform(l.begin(), l.end(), l.begin(), ::tolower);

  if (l == "idle")
    return IOPRIO_CLASS_IDLE;
  if (l == "be" || l == "besteffort" || l == "best effort")
    return IOPRIO_CLASS_BE;
  if (l == "rt" || l == "realtime" || l == "real time")
    return IOPRIO_CLASS_RT;
  return -EINVAL;
}
