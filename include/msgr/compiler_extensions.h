#ifndef COMPILER_EXTENSIONS_e727daef4a184fe0b701e27c9541606f_H
#define COMPILER_EXTENSIONS_e727daef4a184fe0b701e27c9541606f_H

/* We should be able to take advantage of nice nonstandard features of gcc
 * and other compilers, but still maintain portability.
 */

#ifdef __GNUC__

#define WARN_UNUSED_RESULT  __attribute__((warn_unused_result))
#define AEGIS_UNUSED        __attribute__((unused))

#define likely(x)           __builtin_expect((x),1)
#define unlikely(x)         __builtin_expect((x),0)

#else

// some other compiler - just make it a no-op
#define WARN_UNUSED_RESULT
#define likely(x)           (x)
#define unlikely(x)         (x)
#define AEGIS_UNUSED

#endif

#endif // COMPILER_EXTENSIONS_e727daef4a184fe0b701e27c9541606f_H
