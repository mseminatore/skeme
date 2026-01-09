#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"

// symbol table
Symbol_t symbols[MAX_SYMBOLS];
int symbol_count = 0;
int lineno = 1;

int lookahead = 0;

YYLVAL yylval;

// file descriptor stack
#define MAX_FILE_STACK 50

int file_stack_ptr = 0;
FileNode file_nodes[MAX_FILE_STACK];

// define our keyword token table
Tokens tokens[] =
{
    {"()",      NIL },
	{"nil",     NIL },
    {"#t",      TOKEN_TRUE },
    {"true",    TOKEN_TRUE },
    {"#f",      TOKEN_FALSE },
    {"false",   TOKEN_FALSE },
    {"define",  DEFINE},
    {"lambda",  LAMBDA},
    {"list",    LIST },
	{"set!",    SET },
    {"if",      IF },
    {"and",     AND },
    {"or",      OR },
    {"cond",    COND },
    {"else",    ELSE },
    // { "begin",   BEGIN },

    // { "case",    CASE_TOKEN },
    // { "let",     LET },
    // { "quote",   QUOTE },
    // { "eval",    EVAL },
    { NULL, 0}
};

//-----------------------------------
// logging function
//-----------------------------------
void LOG(const char *fmt, ...)
{
    if (!g_bDebug)
    {
        return;
    }
    
	char buf[BUF_SIZE];
	va_list valist;

	va_start(valist, fmt);
	vsprintf(buf, fmt, valist);
	va_end(valist);

	fputs(buf, stdout);
}

//-----------------------------------
// push new input file onto the stack
//-----------------------------------
int push_fileptr_stack(FILE *fptr, const char* filename)
{
    if (file_stack_ptr + 1 > MAX_FILE_STACK)
    {
        yyerror("too many files!");
        return FALSE;
    }

    file_stack_ptr++;
    file_nodes[file_stack_ptr].fptr = fptr;
    file_nodes[file_stack_ptr].filename = strdup(filename);
    file_nodes[file_stack_ptr].yylineno = 1;
    file_nodes[file_stack_ptr].column = 1;

    yyin = fptr;
    LOG("Push '%s' onto parser input stack (depth = %d)\n", filename, file_stack_ptr);
    return TRUE;
}

//-----------------------------------
// push new input file onto the stack
//-----------------------------------
int push_filename_stack(const char* filename)
{
    FILE* fptr = fopen(filename, "rt");
    if (!fptr)
    {
        yyerror("file not found!");
        return FALSE;
    }

    int result = push_fileptr_stack(fptr, filename);
    if (!result && fptr != stdin)
    {
        fclose(fptr);
    }

    return result;
}

//-----------------------------------
// pop input file off the stack
//-----------------------------------
int pop_file_stack()
{
    if (file_stack_ptr <= 0)
    {
        yyerror("file stack underflow!");
        return FALSE;
    }

    LOG("Popping '%s' from parser input stack (depth = %d)\n", file_nodes[file_stack_ptr].filename, file_stack_ptr-1);

    free(file_nodes[file_stack_ptr].filename);

    if (file_nodes[file_stack_ptr].fptr != stdin)
        fclose(file_nodes[file_stack_ptr].fptr);
    else
    {
        yyerror("attempt to close stdin!");
        return FALSE;
    }

    file_stack_ptr--;
    yyin = file_nodes[file_stack_ptr].fptr;

    if (NULL == yyin || stdin == yyin)
        return FALSE;

    return TRUE;
}

//------------------------
// lookup a symbol
//------------------------
int lookup_symbol(const char *name)
{
    for (int i = 0; i < symbol_count; i++)
    {
        if (!strcasecmp(name, symbols[i].name))
            return i;
    }

    // symbol not found!
    return -1;
}

//-----------------------------------------
// add a new symbols, return symbol number
//-----------------------------------------
int add_symbol(const char *name, int lineno)
{
    assert(symbol_count < MAX_SYMBOLS);

    // error if symbol already exists
    if (lookup_symbol(name) > 0)
        return -1;

    symbols[symbol_count].name      = strdup(name);
    symbols[symbol_count].lineno    = CURRENT_LINENO;
    symbols[symbol_count].filename  = CURRENT_FILE ? strdup(CURRENT_FILE) : "noname";
    symbols[symbol_count].type      = ST_UNDEF;
    symbols[symbol_count].value     = -1;
    symbols[symbol_count].refd      = 0;

    symbol_count++;
    return symbol_count - 1;
}

//-----------------------------------
// next next char from input stream
//-----------------------------------
int getch()
{
    return fgetc(yyin);
}

//-----------------------------------
// put char back to input stream
//-----------------------------------
int ungetch(int c)
{
    return ungetc(c, yyin);
}

//---------------------------------------------------
// conditionally return values, based on whether token 
// matches an expected value
//---------------------------------------------------
int follow(int expect, int ifyes, int ifno)
{
	int chr;

	chr = getch();
	if (chr == expect)
		return ifyes;

	ungetch(chr);
	return ifno;
}

//------------------------
// see if we match a token
//------------------------
int isToken(const char *s)
{
    Tokens *pTokens = tokens;

    for (; pTokens->lexeme != NULL; pTokens++)
    {
        if (!strcasecmp(s, pTokens->lexeme))
            return pTokens->token;
    }

    return FALSE;
}

//------------------------
// discard input to EOL
//------------------------
void skipToEOL(void)
{
	int c;

	// skip to EOL
	do {
		c = getch();
	} while (c != '\n' && c != EOF);

	// put last character back
	ungetch(c);
}

//------------------------
// match a number token
//------------------------
int getNumber()
{
	int c;
	char buf[BUF_SIZE];
	char *bufptr = buf;
	int base = 10;

	// look for hex numbers
 	c = getch();

    if (c == '-' || c == '+')
    {
		*bufptr++ = c;
        c = getch();
    }

	if (c == '$' || (c == '0' && (follow('X', TRUE, FALSE) || follow('x', TRUE, FALSE))))
		base = 16;
	else
		ungetch(c);

	if (base == 16)
	{
		while (isxdigit(c = getch()))
			*bufptr++ = c;
	}
	else
	{
		while (isdigit((c = getch())) || c == '.')
			*bufptr++ = c;
	}
	
	// need to put back the last character
	ungetch(c);

	// make sure string is asciiz
	*bufptr = '\0';

    yylval.ival = strtol(buf, NULL, base);
    return NUMBER;
}

//-------------------------------
// translate backslash characters
//-------------------------------
int backslash(int c)
{
	static char translation_tab[] = "b\bf\fn\nr\rt\t";

	if (c != '\\')
		return c;

	c = getch();
	if (islower(c) && strchr(translation_tab, c))
		return strchr(translation_tab, c)[1];

	return c;
}

//------------------------
// match a string literal
//------------------------
int getString(int delim)
{
    int c;
    char buf[BUF_SIZE], *cptr = buf;

    c = getch();

    while (c != delim && cptr < &buf[sizeof(buf)])
    {
        if (c == '\n' || c == EOF)
            yyerror("missing end quote");

        *cptr++ = backslash(c);
        c = getch();
    }
    
    *cptr = 0;

    // lookup symbol and return if exists
    int sym_num = lookup_symbol(buf);
    if (sym_num != -1)
    {
        yylval.symbol = sym_num;
        // LOG("Existing symbol ref'd: '%s' (index = %d)\n", buf, sym_num);
        return STRING;
    }

    sym_num = add_symbol(buf, lineno);

    yylval.symbol = sym_num;
    //strcpy(yylval.word, buf);
    // LOG("New symbol: '%s' (index = %d)\n", buf, sym_num);

    return STRING;
}

//------------------------
// lexical analyzer
//------------------------
int yylex()
{
    int c;

yylex01:
    // skip leading whitespace
    while ((c = getch()) == ' ' || c == '\t');

    // see if input stack is empty
    if (c == EOF)
    {
        if (pop_file_stack())
        {
            goto yylex01;
        }

        return DONE;
    }

    // look for comments
    if (c == ';')
    {
        skipToEOL();
        goto yylex01;
    }

    // look for char literals
    if (c == '\'')
    {
        c = getch();
        if (follow('\'', TRUE, FALSE))
        {
            yylval.ival = backslash(c);
            return CHAR;
        }

        ungetch(c);
        return getString('\'');
    }

    // look for empty lists
    if (c == '(' && follow(')', TRUE, FALSE))
        return NIL;

    // look for string literals
    if (c == '"')
    {
        return getString('"');
    }

	// look for a number value
	if (isdigit(c) || c == '-' || c == '+' || c == '$')
	{
        if (c == '-' || c == '+')
        {
            int n = getch();
            ungetch(n);

            if (n != '$' && !isdigit(n))
            {
                yylval.word[0] = c; yylval.word[1] = 0;
                return c;
            }
        }

		ungetch(c);
		return getNumber();
	}

    // look for start of a token
    if (c == '#' || isalpha(c))
    {
        char buf[BUF_SIZE], *p = buf;

        do {
            *p++ = c;
        } while ((c=getch()) != EOF && (c == '_' || c == '?' || c == '-' || c == '!' || isalnum(c)));
        
        // put back the last character!
        ungetch(c);

        // be sure to null terminate the string
        *p = 0;

        int token = isToken(buf);
        if (token)
        {
            return token;
        }
        
        // lookup symbol and return if exists
        int sym_num = lookup_symbol(buf);
        if (sym_num == -1)
        {
            sym_num = add_symbol(buf, lineno);
            // LOG("New symbol: '%s' (index = %d)\n", buf, sym_num);
        }
        else
        {
            // LOG("Existing symbol ref'd: '%s' (index = %d)\n", buf, sym_num);
        }

        yylval.symbol = sym_num;
        strcpy(yylval.word, buf);
        return ID;
    }

    // track line numbers
    if (c == '\n')
    {
        lineno++;           // update global line count
        CURRENT_LINENO++;   // update file local line number
        
        if (yyin == stdin)
            return DONE;

        goto yylex01;
    }

    switch (c)
    {
    case '<':
        if (follow('=', TRUE, FALSE))
        {
			strcpy(yylval.word, "<=");
            return ID;
        }
        break;

    case '>':
        if (follow('=', TRUE, FALSE))
        {
            strcpy(yylval.word, ">=");
            return ID;
        }
        break;
    }

    // return single character tokens
    yylval.word[0] = c; yylval.word[1] = 0;
    return c;
}
