#ifndef __MODULES_H
#define __MODULES_H

#include "parser.h"

// Module initializer function type
// Called once when (import name) is first executed.
// Should register functions via sk_register_functions() and
// optionally define global constants.
typedef int (*SKModuleInitFn)(SKState* pSK);

// Module descriptor
typedef struct {
    const char* name;       // module name used in (import name)
    SKModuleInitFn init;    // initialization function
} SKModule;

// Import a module by name. Returns 0 on success, -1 if not found.
// Safe to call multiple times — tracks loaded modules to avoid double-init.
int sk_import_module(SKState* pSK, const char* name);

// Module init functions (defined in mod_*.c files)
int sk_math_init(SKState* pSK);

#endif
