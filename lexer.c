#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include "lexer.h"
#include "parser.h"

// current state
int lineno = 1;
int lookahead = 0;
YYLVAL yylval;

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
    {"load",    LOAD },
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
int push_fileptr_stack(SKState* pSK, FILE *fptr, const char* filename)
{
    if (!pSK) return FALSE;

    if (pSK->file_stack_ptr + 1 >= pSK->file_stack_capacity)
    {
        // grow file stack
        int new_capacity = pSK->file_stack_capacity * 2;
        FileNode* new_stack = (FileNode*)realloc(pSK->file_stack, new_capacity * sizeof(FileNode));
        if (!new_stack)
        {
            yyerror("failed to grow file stack!");
            return FALSE;
        }
        pSK->file_stack = new_stack;
        pSK->file_stack_capacity = new_capacity;
    }

    pSK->file_stack_ptr++;
    pSK->file_stack[pSK->file_stack_ptr].fptr = fptr;
    pSK->file_stack[pSK->file_stack_ptr].filename = strdup(filename);
    pSK->file_stack[pSK->file_stack_ptr].yylineno = 1;
    pSK->file_stack[pSK->file_stack_ptr].column = 1;

    yyin = fptr;
    LOG("Push '%s' onto parser input stack (depth = %d)\n", filename, pSK->file_stack_ptr);
    return TRUE;
}

//-----------------------------------
// push new input file onto the stack
//-----------------------------------
int push_filename_stack(SKState* pSK, const char* filename)
{
    FILE* fptr = fopen(filename, "rt");
    if (!fptr)
    {
        yyerror("file not found!");
        return FALSE;
    }

    int result = push_fileptr_stack(pSK, fptr, filename);
    if (!result && fptr != stdin)
    {
        fclose(fptr);
    }

    return result;
}

//-----------------------------------
// pop input file off the stack
//-----------------------------------
int pop_file_stack(SKState* pSK)
{
    if (!pSK) return FALSE;

    if (pSK->file_stack_ptr < 0)
    {
        yyerror("file stack underflow!");
        return FALSE;
    }

    LOG("Popping '%s' from parser input stack (depth = %d)\n", pSK->file_stack[pSK->file_stack_ptr].filename, pSK->file_stack_ptr-1);

    free(pSK->file_stack[pSK->file_stack_ptr].filename);

    if (pSK->file_stack[pSK->file_stack_ptr].fptr != stdin)
        fclose(pSK->file_stack[pSK->file_stack_ptr].fptr);
    else
    {
        yyerror("attempt to close stdin!");
        return FALSE;
    }

    pSK->file_stack_ptr--;
    
    if (pSK->file_stack_ptr >= 0)
        yyin = pSK->file_stack[pSK->file_stack_ptr].fptr;
    else
        yyin = NULL;

    if (NULL == yyin || stdin == yyin)
        return FALSE;

    return TRUE;
}

//------------------------
// lookup a symbol
//------------------------
int lookup_symbol(SKState* pSK, const char *name)
{
    if (!pSK) return -1;

    for (int i = 0; i < pSK->symbol_count; i++)
    {
        if (!strcasecmp(name, pSK->symbols[i].name))
            return i;
    }

    // symbol not found!
    return -1;
}

//-----------------------------------------
// add a new symbols, return symbol number
//-----------------------------------------
int add_symbol(SKState* pSK, const char *name, int lineno)
{
    if (!pSK) return -1;

    // grow symbol table if needed
    if (pSK->symbol_count >= pSK->symbol_capacity)
    {
        int new_capacity = pSK->symbol_capacity * 2;
        Symbol_t* new_symbols = (Symbol_t*)realloc(pSK->symbols, new_capacity * sizeof(Symbol_t));
        if (!new_symbols)
        {
            yyerror("failed to grow symbol table!");
            return -1;
        }
        pSK->symbols = new_symbols;
        pSK->symbol_capacity = new_capacity;
    }

    // error if symbol already exists
    if (lookup_symbol(pSK, name) > 0)
        return -1;

    pSK->symbols[pSK->symbol_count].name      = strdup(name);
    pSK->symbols[pSK->symbol_count].lineno    = CURRENT_LINENO;
    pSK->symbols[pSK->symbol_count].filename  = CURRENT_FILE ? strdup(CURRENT_FILE) : "noname";
    pSK->symbols[pSK->symbol_count].type      = ST_UNDEF;
    pSK->symbols[pSK->symbol_count].value     = -1;
    pSK->symbols[pSK->symbol_count].refd      = 0;

    pSK->symbol_count++;
    return pSK->symbol_count - 1;
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
	static char translation_tab[] = "a\ab\bf\fn\nr\rt\te\x1b";

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
int getString(SKState* pSK, int delim)
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
    int sym_num = lookup_symbol(pSK, buf);
    if (sym_num != -1)
    {
        yylval.symbol = sym_num;
        // LOG("Existing symbol ref'd: '%s' (index = %d)\n", buf, sym_num);
        return STRING;
    }

    sym_num = add_symbol(pSK, buf, lineno);

    yylval.symbol = sym_num;
    //strcpy(yylval.word, buf);
    // LOG("New symbol: '%s' (index = %d)\n", buf, sym_num);

    return STRING;
}

//------------------------
// lexical analyzer
//------------------------
int yylex(SKState* pSK)
{
    int c;

yylex01:
    // skip leading whitespace
    while ((c = getch()) == ' ' || c == '\t');

    // see if input stack is empty
    if (c == EOF)
    {
        if (pop_file_stack(pSK))
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
        if (c == '(' && follow(')', TRUE, FALSE))
        {
            return NIL;
        }
        else if (follow('\'', TRUE, FALSE))
        {
            yylval.ival = backslash(c);
            return CHAR;
        }

        ungetch(c);
        return getString(pSK, '\'');
    }

    // look for empty lists
    if (c == '(' && follow(')', TRUE, FALSE))
        return NIL;

    // look for string literals
    if (c == '"')
    {
        return getString(pSK, '"');
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
    if (c == '#' || isalpha(c) || c == '_')
    {
        char buf[BUF_SIZE], *p = buf;

        do {
            *p++ = c;
        } while ((c=getch()) != EOF && (c == '_' || c == '?' || c == '-' || c == '!' || c == '>' || c == '<' || isalnum(c)));
        
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
        int sym_num = lookup_symbol(pSK, buf);
        if (sym_num == -1)
        {
            sym_num = add_symbol(pSK, buf, lineno);
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

    // handle two-character tokens
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
