#include <stdlib.h>
#include <strings.h>

#include "msgr/environment.h"

bool get_env_bool(const char *key)
{
    const char *val = getenv(key);
    if (!val)
        return false;
    if (strcasecmp(val, "off") == 0)
        return false;
    if (strcasecmp(val, "no") == 0)
        return false;
    if (strcasecmp(val, "false") == 0)
        return false;
    if (strcasecmp(val, "0") == 0)
        return false;
    return true;
}

int get_env_int(const char *key)
{
    const char *val = getenv(key);
    if (!val)
        return 0;
    int v = atoi(val);
    return v;
}
