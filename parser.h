#ifndef __PARSER_H
#define __PARSER_H

#include "lexer.h"
#include "vm.h"

//-----------------
// constant decls
//-----------------
#define APP_NAME "skeme"
#define APP_VER "0.1"

// remove this later
#define YYDEBUG 1

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState *pSK);

typedef struct
{
	char *name;
	SKFunc func;
} SKFuncSet;

struct SKState
{
	int unused;
    // VM vm;
};

extern VM vm;

//-----------------
// function decls
//-----------------
void push_file_stack(const char *filename);
void pop_file_stack(void);
int yyparse(void);
void dump_symbols();

int sk_init(SKState *pSK);
int sk_parse();
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_display(SKState *pSK);

extern int err_count;
extern int warn_count;

#endif // __PARSER_H