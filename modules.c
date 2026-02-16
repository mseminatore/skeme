#include "modules.h"
#include <string.h>
#include <stdio.h>

// Compile-time module registry — add new modules here
static const SKModule module_registry[] = {
    { "math", sk_math_init },
    { NULL, NULL }  // sentinel
};

// Track loaded modules to avoid double-init
#define MAX_MODULES 32
static const char* loaded_modules[MAX_MODULES];
static int loaded_count = 0;

static int is_module_loaded(const char* name)
{
    for (int i = 0; i < loaded_count; i++)
    {
        if (strcmp(loaded_modules[i], name) == 0)
            return 1;
    }
    return 0;
}

int sk_import_module(SKState* pSK, const char* name)
{
    if (!pSK || !name)
        return -1;

    // Already loaded — skip silently
    if (is_module_loaded(name))
        return 0;

    // Look up in registry
    for (int i = 0; module_registry[i].name; i++)
    {
        if (strcmp(module_registry[i].name, name) == 0)
        {
            int result = module_registry[i].init(pSK);
            if (result == 0 && loaded_count < MAX_MODULES)
            {
                loaded_modules[loaded_count++] = module_registry[i].name;
            }
            return result;
        }
    }

    fprintf(stderr, "ERROR: unknown module '%s'\n", name);
    return -1;
}
