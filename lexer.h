//---------------------------------------------------------
// lexer.h - definitions for the lexer for SK
//
// Copyright (c) 2025 Mark Seminatore
//----------------------------------------------------------

#ifndef __LEXER_H
#define __LEXER_H

#include <stdio.h>
#include "platform.h"

#define DONE 0

#define SMALL_BUF   64
#define BUF_SIZE    512

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

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
    char word[BUF_SIZE];   // storage for symbol/string name
} YYLVAL;

// input file ptr
extern FILE *yyin;

// token values
enum { ID = 256, DEFINE, LAMBDA, NUMBER, STRING, CHAR, NIL, TOKEN_TRUE, TOKEN_FALSE, IF, ELSE, COND, LET, LET_STAR, LETREC, BEGIN, QUOTE, SET,
       LIST, CASE_TOKEN, AND, OR, LOAD, IMPORT, FLOAT, DO,
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

#endif // __LEXER_H
