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

#define LAST_SYMBOL       symbols[symbol_count - 1]
#define CURRENT_FILE      file_nodes[file_stack_ptr].filename
#define CURRENT_LINENO    file_nodes[file_stack_ptr].yylineno

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
    ST_UNDEF,   // symbol is undefined
    ST_EQU,     // symbol is an EQUate
    ST_SET,     // symbol is a set value
    ST_LABEL,   // symbol is an address label
    ST_STRING   // symbol is a string literal
} SYMBOL_TYPE;

// symbol structure def'n
typedef struct
{
    const char *name;
    const char *filename;   // file where symbols was defined
    int type;
    int value;
    int lineno;             // local file line number
    int refd;               // was symbol referenced?
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

enum { ID = 256, NUMBER, STRING, CHAR, LAMBDA, DEFINE, IF, ELSE, COND, LET, BEGIN, QUOTE, SET,
       CAR, CDR, CONS, LIST, NULLP, NUMBERP, SYMBOLP, PRINT, READ, EVAL, EXIT };

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
void yyerror(char*);

#endif // __LEXER_H
