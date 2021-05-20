#ifndef INTARITH_5b75c9db6d064600b305fa1a50b000f2_H
#define INTARITH_5b75c9db6d064600b305fa1a50b000f2_H

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a):(b))
#endif

#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a):(b))
#endif

#ifndef DIV_ROUND_UP
# define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
#endif

#ifndef ROUND_UP_TO
# define ROUND_UP_TO(n, d) ((n)%(d) ? ((n)+(d)-(n)%(d)) : (n))
#endif

#ifndef SHIFT_ROUND_UP
# define SHIFT_ROUND_UP(x,y) (((x)+(1<<(y))-1) >> (y))
#endif

#endif // INTARITH_5b75c9db6d064600b305fa1a50b000f2_H
