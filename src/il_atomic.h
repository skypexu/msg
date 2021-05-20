#ifndef IL_ATOMIC_H
#define IL_ATOMIC_H

#ifdef __i386__

static inline int
il_cas_int(volatile int *dst, int expect, int src)
{
	unsigned char res;

	__asm __volatile(
	"       lock;                   "
	"       cmpxchgl %2,%1 ;        "
	"       sete    %0 ;            "
	"1:                             "
	"# atomic_cmpset_int"
	: "=a" (res),                   /* 0 */
	  "=m" (*dst)                   /* 1 */
	: "r" (src),                    /* 2 */
	  "a" (expect),                 /* 3 */
	  "m" (*dst)                    /* 4 */
	: "memory", "cc");

	return (res);
}

static inline int
il_cas_acq_int(volatile int *dst, int expect, int src)
{
	return il_cas_int(dst, expect, src);
}

static inline int
il_cas_rel_int(volatile int *dst, int expect, int src)
{
	return il_cas_int(dst, expect, src);
}
#endif

#ifdef __x86_64__

static inline int
il_cas_int(volatile int *dst, int expect, int src)
{
	char res;

	__asm __volatile(
        "       lock;                   "
	"       cmpxchgl %2,%1 ;        "
	"       sete    %0 ;            "
	"1:                             "
	"# atomic_cmpset_int"
	: "=a" (res),                   /* 0 */
	  "=m" (*dst)                   /* 1 */
	: "r" (src),                    /* 2 */
	  "a" (expect),                 /* 3 */
	  "m" (*dst)                    /* 4 */
	: "memory", "cc");

	return (res);
}

static inline int
il_cas_acq_int(volatile int *dst, int expect, int src)
{
	return il_cas_int(dst, expect, src);
}

static inline int
il_cas_rel_int(volatile int *dst, int expect, int src)
{
	return il_cas_int(dst, expect, src);
}

#endif

#endif
