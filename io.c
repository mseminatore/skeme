#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

#ifdef __linux
    #include <limits.h> // for PATH_MAX
#endif

#ifdef __APPLE__
    #include <sys/syslimits.h> // for PATH_MAX
#endif

#ifdef _WIN32
    #include <direct.h> // for _getcwd
#else
    #include <unistd.h> // for getcwd
#endif

#ifdef _WIN32
    #define PATH_MAX _MAX_PATH
    #define getcwd _getcwd
#endif

