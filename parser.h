#ifndef __PARSER_H
#define __PARSER_H

#include <stdio.h>

//-----------------
// constant decls
//-----------------
#define APP_NAME "skeme"
#define APP_VER "0.1"

#define DONE 0
#define MAX_SYMBOLS 4096

#define BUF_SIZE    512

#ifdef _WIN32
#   define strcasecmp _stricmp
#   define strdup _strdup
#endif

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

// remove this later
#define YYDEBUG 1

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

//-----------------
// function decls
//-----------------
int yylex();
void yyerror(char*);
void push_file_stack(const char *filename);
void pop_file_stack(void);
int yyparse(void);

#endif // __PARSER_H