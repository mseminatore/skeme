#ifndef __PARSER_H
#define __PARSER_H

#include <string.h>
#include <stdio.h>
#include "lexer.h"
#include "vm.h"
#include "error.h"

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
    VM vm;                          // the virtual machine
    Symbol_t* symbols;              // the symbol table. Note: dynamic array that should go away later
    int symbol_count;               // number of symbols in table
    int symbol_capacity;            // capacity of symbol table
    FileNode* file_stack;           // stack of input files
    int file_stack_ptr;             // current file stack pointer
    int file_stack_capacity;        // capacity of file stack
    int err_count;                  // number of errors encountered
    int warn_count;                 // number of warnings encountered
    int parse_log_enabled;          // enable parse logging
    SKErrorHandler error_handler;   // error handler function pointer
    FILE* error_stream;             // error output stream
    FILE* output_stream;            // general output stream
    FILE *yyin;                     // current input file pointer

    int indent_level;              // current parse indent level
    int expr_level;                // current expression nesting level
    int bDone;                     // flag to signal interpreter shutdown
    int lookahead;                 // current lookahead token

};

//-----------------
// function decls
//-----------------
int yyparse(SKState *pSK);
void dump_symbols(SKState* pSK);
void yyerror(SKState* pSK, const char *s);
void yywarning(SKState* pSK, const char *s);
void yyinfo(SKState* pSK, const char *s);

int sk_register_functions(SKState *pSK, const SKFuncSet funcs[]);
int sk_display(SKState *pSK);



#endif // __PARSER_H