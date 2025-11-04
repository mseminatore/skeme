#ifndef __LEXER_H
#define __LEXER_H

#include <stdio.h>

#define DONE 0
#define MAX_SYMBOLS 4096

#define BUF_SIZE    512

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

#ifdef _WIN32
#   define strcasecmp _stricmp
#   define strdup _strdup
#endif

#define LAST_SYMBOL       symbols[symbol_count - 1]
#define CURRENT_FILE      file_nodes[file_stack_ptr].filename
#define CURRENT_LINENO    file_nodes[file_stack_ptr].yylineno

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

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
    const char *name;
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

typedef struct 
{
    int ival;
    int symbol;
    char *lexeme;
} YYLVAL;

extern FILE *yyin;

enum { ID = 256, DEFINE, LAMBDA, NUMBER, STRING, CHAR, NIL, TOKEN_TRUE, TOKEN_FALSE, IF, ELSE, COND, LET, BEGIN, QUOTE, SET,
       LIST, SYMBOLP, PRINT, READ, EVAL, EXIT };

extern int lookahead;
extern int symbol_count;
extern YYLVAL yylval;
extern Symbol_t symbols[];
extern int lineno;
extern int g_bDebug;
extern int file_stack_ptr;
extern FileNode file_nodes[];

int yylex();
void LOG(const char *fmt, ...);
void yyerror(const char*);
int add_symbol(const char* name, int lineno);
int lookup_symbol(const char* name);

#endif // __LEXER_H
