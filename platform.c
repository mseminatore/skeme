#include "platform.h"
#include <string.h>
#include <stdlib.h>

// Platform-specific includes
#ifdef SK_PLATFORM_WINDOWS
#   include <direct.h>
#else
#   include <unistd.h>
#   include <strings.h>
#endif

//-----------------------------------------------
// sk_strcasecmp - case-insensitive string compare
//-----------------------------------------------
int sk_strcasecmp(const char* s1, const char* s2)
{
#ifdef SK_PLATFORM_WINDOWS
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

//-----------------------------------------------
// sk_strdup - duplicate a string
//-----------------------------------------------
char* sk_strdup(const char* s)
{
#ifdef SK_PLATFORM_WINDOWS
    return _strdup(s);
#else
    return strdup(s);
#endif
}

//-----------------------------------------------
// sk_getcwd - get current working directory
//-----------------------------------------------
char* sk_getcwd(char* buf, size_t size)
{
#ifdef SK_PLATFORM_WINDOWS
    return _getcwd(buf, (int)size);
#else
    return getcwd(buf, size);
#endif
}
