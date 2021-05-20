#ifndef HASH_NAMESPACE_1ebc032a564a4c68b6f2150b88b67487_H
#define HASH_NAMESPACE_1ebc032a564a4c68b6f2150b88b67487_H

#ifndef USE_TR1

#include <functional>

#define MSGR_HASH_NAMESPACE_START namespace std {
#define MSGR_HASH_NAMESPACE_END }
#define MSGR_HASH_NAMESPACE std

#else
#include <tr1/functional>

#define MSGR_HASH_NAMESPACE_START namespace std { namespace tr1 {
#define MSGR_HASH_NAMESPACE_END }}
#define MSGR_HASH_NAMESPACE std::tr1

#endif

#endif
