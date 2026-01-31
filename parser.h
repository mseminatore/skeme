#ifndef __PARSER_H
#define __PARSER_H

#include "lexer.h"
#include "vm.h"

#define MAX_REL_CLAUSES 16
//#define IF_ELSE_REQUIRED

//-----------------
// Error codes
//-----------------
typedef enum {
    SK_OK = 0,
    SK_ERR_MEMORY,
    SK_ERR_SYMBOL_TABLE_FULL,
    SK_ERR_FILE_STACK_OVERFLOW,
    SK_ERR_INVALID_ARGUMENT,
    SK_ERR_NULL_POINTER
} SKError;

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

#ifndef SK_NULL
	#define SK_NULL NULL
#endif

#define SK_ARITY_ANY -1
#define SK_ARITY_ZERO_OR_ONE -2
#define SK_ARITY_ONE_OR_MORE -3
#define SK_ARITY_TWO_OR_MORE -4

#define SK_UNKNOWN_FILE "<unknown>"
#define SK_NONAME_FILE "noname"

// function set structure
typedef struct
{
	char *name;
	SKFunc func;
	int arity;
} SKFuncSet;

// skeme state structure
struct SKState
{
    VM vm;
    Symbol_t* symbols;
    int symbol_count;
    int symbol_capacity;
    FileNode* file_stack;
    int file_stack_ptr;
    int file_stack_capacity;
    int err_count;
    int warn_count;
    int parse_log_enabled;
};

extern int g_bDone;

//-----------------
// function decls
//-----------------
int yyparse(SKState *pSK);
void dump_symbols(SKState* pSK);
void yyerror(const char *s);
void yywarning(const char *s);
void yyinfo(const char *s);

int sk_init(SKState *pSK, int argc, char *argv[]);
int sk_shutdown(SKState *pSK);
int sk_parse(SKState *pSK);
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_display(SKState *pSK);



#endif // __PARSER_H