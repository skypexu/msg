#include <sstream>
#include <string.h>

#include "msgr/cpp_errno.h"

/* XSI-compliant */
typedef int (*int_strerror_r)(int errnum, char *buf, size_t buflen);

template <typename Fn>
char *mystrerror_r(Fn f, int errnum, char *buf, size_t buflen)
{
    return f(errnum, buf, buflen);
}

template <int_strerror_r>
char *mystrerror_r(int_strerror_r f, int errnum, char *buf, size_t buflen)
{
    if (f(errnum, buf, buflen))
        buf[0] = '\0';
    return buf;
}

std::string cpp_strerror(int err)
{
    char buf[128];
    char *errmsg;

    if (err < 0)
        err = -err;
    std::ostringstream oss;
    buf[0] = '\0';

    errmsg = mystrerror_r(strerror_r, err, buf, sizeof(buf));;
    oss << "(" << err << ") " << errmsg;

    return oss.str();
}
