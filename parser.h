#ifndef __PARSER_H
#define __PARSER_H

#include "lexer.h"
#include "vm.h"

#define MAX_REL_CLAUSES 16

//-----------------
// constant decls
//-----------------
#define APP_NAME "skeme"
#define APP_VER "0.1"

// remove this later
#define YYDEBUG 1

#ifndef SK_TRUE
	#define SK_TRUE 1
#endif

#ifndef SK_FALSE
	#define SK_FALSE 0
#endif

// function set structure
typedef struct
{
	char *name;
	SKFunc func;
	//int arity;
} SKFuncSet;

// skeme state structure
struct SKState
{
	int unused;
    // VM vm;
};

extern VM vm;
extern int g_bDone;
extern int g_bParseLog;

//-----------------
// function decls
//-----------------
void push_file_stack(const char *filename);
void pop_file_stack(void);
int yyparse(void);
void dump_symbols();
void yyerror(const char *s);
void yywarning(const char *s);
void yyinfo(const char *s);

int sk_init(SKState *pSK);
int sk_parse();
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_display(SKState *pSK);

extern int err_count;
extern int warn_count;

#endif // __PARSER_H