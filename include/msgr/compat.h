#ifndef COMPAT_f2ca3e3738bf4e59a111ab2c43d5aef2_H
#define COMPAT_f2ca3e3738bf4e59a111ab2c43d5aef2_H

#if defined(__FreeBSD__)
#define	ENODATA	61
#define	MSG_MORE 0
#endif /* !__FreeBSD__ */

#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression)           \
  (__extension__                                  \
    ({ long int __result;                         \
       do __result = (long int) (expression);     \
       while (__result == -1L && errno == EINTR); \
       __result; }))                                                            
#endif

#ifdef __cplusplus
# define VOID_TEMP_FAILURE_RETRY(expression) \
   static_cast<void>(TEMP_FAILURE_RETRY(expression))
#else
# define VOID_TEMP_FAILURE_RETRY(expression) \
   do { (void)TEMP_FAILURE_RETRY(expression); } while (0)
#endif

#if defined(__FreeBSD__) || defined(__APPLE__)
#define lseek64(fd, offset, whence) lseek(fd, offset, whence)
#endif

#endif /* COMPAT_f2ca3e3738bf4e59a111ab2c43d5aef2_H */
