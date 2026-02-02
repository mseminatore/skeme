//
//
//

#include "sk.h"

//-----------------------------------
// define a variable
//-----------------------------------
int sk_define(SKState* pSK, const char *key, Object *value)
{
    if (!pSK) return SK_FALSE;

    vm_push(&pSK->vm, value);
    vm_define_global(&pSK->vm, (Object*)vm_new_string(&pSK->vm, key), value);
    vm_pop(&pSK->vm);

    return SK_TRUE;
}
