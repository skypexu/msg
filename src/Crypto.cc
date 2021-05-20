#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "msgr/Crypto.h"
#include "msgr/compat.h"
#include "msgr/safe_io.h"

namespace msgr {

int get_random_bytes(char *buf, int len)
{
  int fd = TEMP_FAILURE_RETRY(::open("/dev/urandom", O_RDONLY));
  if (fd < 0)
    return -errno;
  int ret = safe_read_exact(fd, buf, len);
  VOID_TEMP_FAILURE_RETRY(::close(fd));
  return ret;
}

#if 0
static int get_random_bytes(int len, bufferlist& bl)
{
  char buf[len];
  get_random_bytes(buf, len);
  bl.append(buf, len);
  return 0;
}
#endif

uint64_t get_random(uint64_t min_val, uint64_t max_val)
{
  uint64_t r;
  get_random_bytes((char *)&r, sizeof(r));
  r = min_val + r % (max_val - min_val + 1);
  return r;
}

}

