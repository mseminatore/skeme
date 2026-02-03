//
// sk.h - public header for SK project
//

#ifndef __SK_H
#define __SK_H

#include "parser.h"

int sk_define(SKState* pSK, const char *key, Object *value);
int sk_init(SKState *pSK, int argc, char *argv[]);
int sk_shutdown(SKState *pSK);
int sk_parse(SKState *pSK);
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_display(SKState *pSK);

#endif // __SK_H
