#ifndef __LEXER_H
#define __LEXER_H

#include <stdio.h>
#include "platform.h"

#define DONE 0
#define MAX_SYMBOLS 4096

#define SMALL_BUF   64
#define BUF_SIZE    512

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

#define LAST_SYMBOL       (pSK->symbols[pSK->symbol_count - 1])
#define CURRENT_FILE      (pSK->file_stack[pSK->file_stack_ptr].filename)
#define CURRENT_LINENO    (pSK->file_stack[pSK->file_stack_ptr].yylineno)

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

// file stack node structure
typedef struct
{
    FILE *fptr;
    int yylineno;
    int column;
    char *filename;    
} FileNode;

// symbol types
typedef enum
{
    ST_UNDEF,       // symbol is undefined
	ST_FUNCTION,    // symbol is a function
	ST_NATIVE,      // symbol is a native function
    ST_STRING       // symbol is a string literal
} SYMBOL_TYPE;

// symbol structure def'n
typedef struct
{
    const char *name;       // symbol name
    const char *filename;   // file where symbols was defined
	int type;               // symbol type
    int value;              // TODO - Object* ?
    int lineno;             // local file line number
    int refd;               // was symbol referenced?
	SKFunc func;            // function pointer for native functions
} Symbol_t;

// token structure def'n
typedef struct 
{
    const char *lexeme;
    int token;
} Tokens;

// yylval structure def'n
typedef struct 
{
    int ival;
    float fval;     // float value for FLOAT tokens
    int symbol;
    char word[SMALL_BUF];   // storage for symbol name
    // Object *str;
//    char *lexeme;
} YYLVAL;

// input file ptr
extern FILE *yyin;

// token values
enum { ID = 256, DEFINE, LAMBDA, NUMBER, STRING, CHAR, NIL, TOKEN_TRUE, TOKEN_FALSE, IF, ELSE, COND, LET, BEGIN, QUOTE, SET,
       LIST, CASE_TOKEN, AND, OR , LOAD, FLOAT,
//       PRINT, READ, EVAL,
};

// externals
extern YYLVAL yylval;
extern int lineno;
extern int g_bDebug;

// function decls
int push_fileptr_stack(SKState* pSK, FILE *fptr, const char* filename);
int push_filename_stack(SKState* pSK, const char *filename);
int pop_file_stack(SKState* pSK);

int yylex(SKState* pSK);
void LOG(const char *fmt, ...);
void yyerror(SKState* pSK, const char*);
int add_symbol(SKState* pSK, const char* name, int lineno);
int lookup_symbol(SKState* pSK, const char* name);

#endif // __LEXER_H
