#ifndef __PARSER_H
#define __PARSER_H

#include "lexer.h"

//-----------------
// constant decls
//-----------------
#define APP_NAME "skeme"
#define APP_VER "0.1"

#ifdef _WIN32
#   define strcasecmp _stricmp
#   define strdup _strdup
#endif

// remove this later
#define YYDEBUG 1

//-----------------
// function decls
//-----------------
void push_file_stack(const char *filename);
void pop_file_stack(void);
int yyparse(void);

#endif // __PARSER_H