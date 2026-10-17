//-------------------------------------
// sk.h - public header for SK project
//
// Copyright (c) 2025 Mark Seminatore
//-------------------------------------

#ifndef __SK_H
#define __SK_H

#include "parser.h"

// settings the skeme command line sets (defined in sk.c)
extern int g_bDebug;
extern int g_bInteractive;
extern int yydebug;
extern const char* g_szOutputFilename;

int sk_define(SKState* pSK, const char *key, Object *value);
int sk_init(SKState *pSK, int argc, char *argv[]);
int sk_shutdown(SKState *pSK);
int sk_parse(SKState *pSK);
int sk_eval_string(SKState *pSK, const char *code);
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_register_type(SKState *pSK, const SKTypeDesc *desc);
int sk_display(SKState *pSK);

// Memory limits, for embedding. With a heap limit (bytes; 0, the default,
// for none), a run whose live data outgrows it stops with an "out of
// memory" error, and the interpreter can go on.
void sk_set_heap_limit(SKState *pSK, size_t bytes);

// If memory really runs out, skeme cannot go on: it calls this handler,
// which may longjmp back to the embedder (which should then shut down and
// re-initialize the interpreter). Without one, or if it returns, skeme
// prints the message and exits.
typedef void (*SKFatalHandler)(SKState *pSK, const char *msg);
void sk_set_fatal_handler(SKState *pSK, SKFatalHandler handler);

#endif // __SK_H
