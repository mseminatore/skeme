//---------------------------------------------------------
// parser.h - definitions for the parser for SK
//
// Copyright (c) 2025 Mark Seminatore
//----------------------------------------------------------

#ifndef __PARSER_H
#define __PARSER_H

#include <string.h>
#include <stdio.h>
#include "lexer.h"
#include "vm.h"
#include "error.h"

#define MAX_REL_CLAUSES 16
#define MAX_UPVALUES 64
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
#define APP_VER "0.5"

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

#define SK_UNKNOWN_FILE "<unknown>"
#define SK_NONAME_FILE "noname"

// function set structure
typedef struct
{
	char *name;
	SKFunc func;
	int arity;
} SKFuncSet;

// Upvalue descriptor used during parsing to track captured variables
typedef struct {
    char name[64];       // variable name (for dedup)
    int scope_depth;     // how many scopes out (1 = immediate parent)
    int local_index;     // slot index in the enclosing scope's locals or upvalues
    int is_upvalue;      // if true, local_index refers to parent's upvalue (not local)
} ParseUpvalue;

// skeme state structure
struct SKState
{
    VM vm;                          // the virtual machine
    FileNode* file_stack;           // stack of input files
    int file_stack_ptr;             // current file stack pointer
    int file_stack_capacity;        // capacity of file stack
    int err_count;                  // number of errors encountered
    int paren_depth;                // '(' minus ')' tokens lexed (for parse error recovery)
    int echo_form;                  // echo the current top-level form's value (REPL input only)
    TokenReplay* replay;            // recorded tokens yylex returns before reading input
    int warn_count;                 // number of warnings encountered
    int parse_log_enabled;          // enable parse logging
    SKErrorHandler error_handler;   // error handler function pointer
    FILE* error_stream;             // error output stream
    FILE* output_stream;            // general output stream
    FILE *yyin;                     // current input file pointer

    // String input for eval (when non-NULL, getch reads from this)
    const char* eval_input;         // string buffer for eval
    int eval_pos;                   // current position in eval_input
    const char* eval_name;          // name errors report for eval_input, e.g. "<eval>"; NULL
                                    // reports the enclosing file (do loop steps)
    int eval_lineno;                // current line in eval_input
    int eval_file_depth;            // file_stack_ptr that eval_input belongs to; a file loaded
                                    // from the string is pushed above it and read first

    int indent_level;               // current parse indent level
    int expr_level;                 // current expression nesting level
    int parse_depth;                // nesting of yyparse: errors while it is 0 come from running code
    Object* define_name;            // name a top-level define bound while compiling (see definition)
    int bDone;                      // flag to signal interpreter shutdown
    int exit_called;                // (exit) ran: it set bDone and stopped the run
    int exit_code;                  // the code it gave
    int lookahead;                  // current lookahead token
    YYLVAL yylval;                  // current token value
    int lineno;                     // current line number
    int column;                     // current column number
    int in_tail_position;           // flag: are we in tail position of a function?
    
    // TCO bytecode patching support
    int last_call_offset;           // offset of last CALL/ICALL in current function body (-1 if none)
    CodeChunk* last_call_chunk;     // chunk where last call was emitted

    // Closure upvalue tracking (used during parsing)
    ParseUpvalue upvalues[MAX_UPVALUES];  // upvalues captured by current lambda
    int upvalue_count;                     // number of upvalues captured
};

//-----------------
// function decls
//-----------------
int yyparse(SKState *pSK);
void yyerror(SKState* pSK, const char *s);
void yywarning(SKState* pSK, const char *s);
void yyinfo(SKState* pSK, const char *s);

#endif // __PARSER_H