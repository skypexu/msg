#ifndef MSGR_FEATURES_H
#define MSGR_FEATURES_H

/*
 * feature bits
 */
#define MSGR_FEATURE_MSG_AUTH	         (1ULL<<1)

/*
 * Features supported.  Should be everything above.
 */
#define MSGR_FEATURES_ALL		        \
	(MSGR_FEATURE_MSG_AUTH        |    \
	 0ULL)

#define MSGR_FEATURES_SUPPORTED_DEFAULT  MSGR_FEATURES_ALL

static inline unsigned long long msgr_sanitize_features(unsigned long long f) {
    return f;                                                               
}

#endif
