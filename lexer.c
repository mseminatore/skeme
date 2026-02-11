#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include "parser.h"

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
    
    // For backward compatibility, print directly to stdout for debug
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
            yyerror(pSK, "failed to grow file stack!");
            return FALSE;
        }
        pSK->file_stack = new_stack;
        pSK->file_stack_capacity = new_capacity;
    }

    pSK->file_stack_ptr++;
    pSK->file_stack[pSK->file_stack_ptr].fptr = fptr;
    pSK->file_stack[pSK->file_stack_ptr].filename = sk_strdup(filename);
    pSK->file_stack[pSK->file_stack_ptr].yylineno = 1;
    pSK->file_stack[pSK->file_stack_ptr].column = 1;

    pSK->yyin = fptr;
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
        yyerror(pSK, "file not found!");
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

    if (pSK->file_stack_ptr <= 0)
    {
        // Can't pop sentinel entry
        return FALSE;
    }

    LOG("Popping '%s' from parser input stack (depth = %d)\n", pSK->file_stack[pSK->file_stack_ptr].filename, pSK->file_stack_ptr-1);

    free(pSK->file_stack[pSK->file_stack_ptr].filename);

    // Close the file handle, but not stdin
    if (pSK->file_stack[pSK->file_stack_ptr].fptr != stdin &&
        pSK->file_stack[pSK->file_stack_ptr].fptr != NULL)
    {
        fclose(pSK->file_stack[pSK->file_stack_ptr].fptr);
    }

    pSK->file_stack_ptr--;
    
    // Set yyin to the previous file (or NULL if we're back to sentinel)
    if (pSK->file_stack_ptr >= 0)
        pSK->yyin = pSK->file_stack[pSK->file_stack_ptr].fptr;
    else
        pSK->yyin = NULL;

    // Return FALSE if we're back to an invalid state (no file or sentinel)
    if (pSK->file_stack_ptr <= 0 || pSK->yyin == NULL)
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
        if (!sk_strcasecmp(name, pSK->symbols[i].name))
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
            yyerror(pSK, "failed to grow symbol table!");
            return -1;
        }
        pSK->symbols = new_symbols;
        pSK->symbol_capacity = new_capacity;
    }

    // error if symbol already exists
    if (lookup_symbol(pSK, name) > 0)
        return -1;

    pSK->symbols[pSK->symbol_count].name      = sk_strdup(name);
    pSK->symbols[pSK->symbol_count].lineno    = (pSK->file_stack_ptr >= 0) ? CURRENT_LINENO : 0;
    const char* current_file = (pSK->file_stack_ptr >= 0) ? CURRENT_FILE : NULL;
    pSK->symbols[pSK->symbol_count].filename  = current_file ? sk_strdup(current_file) : sk_strdup(SK_NONAME_FILE);
    pSK->symbols[pSK->symbol_count].type      = ST_UNDEF;
    pSK->symbols[pSK->symbol_count].value     = -1;
    pSK->symbols[pSK->symbol_count].refd      = 0;

    pSK->symbol_count++;
    return pSK->symbol_count - 1;
}

//-----------------------------------
// next next char from input stream
//-----------------------------------
int getch(SKState* pSK)
{
    return fgetc(pSK->yyin);
}

//-----------------------------------
// put char back to input stream
//-----------------------------------
int ungetch(SKState* pSK, int c)
{
    return ungetc(c, pSK->yyin);
}

//---------------------------------------------------
// conditionally return values, based on whether token 
// matches an expected value
//---------------------------------------------------
int follow(SKState* pSK, int expect, int ifyes, int ifno)
{
	int chr;

	chr = getch(pSK);
	if (chr == expect)
		return ifyes;

	ungetch(pSK, chr);
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
        if (!sk_strcasecmp(s, pTokens->lexeme))
            return pTokens->token;
    }

    return FALSE;
}

//------------------------
// discard input to EOL
//------------------------
void skipToEOL(SKState* pSK)
{
	int c;

	// skip to EOL
	do {
		c = getch(pSK);
	} while (c != '\n' && c != EOF);

	// put last character back
	ungetch(pSK, c);
}

//------------------------
// match a number token (integer or float)
//------------------------
int getNumber(SKState* pSK)
{
	int c;
	char buf[BUF_SIZE];
	char *bufptr = buf;
	int base = 10;
	int is_float = 0;

	// look for hex numbers
 	c = getch(pSK);

    if (c == '-' || c == '+')
    {
		*bufptr++ = c;
        c = getch(pSK);
    }

	if (c == '$' || (c == '0' && (follow(pSK, 'X', TRUE, FALSE) || follow(pSK, 'x', TRUE, FALSE))))
		base = 16;
	else
		ungetch(pSK, c);

	if (base == 16)
	{
		while (isxdigit(c = getch(pSK)))
			*bufptr++ = c;
	}
	else
	{
		while (isdigit((c = getch(pSK))) || c == '.')
		{
			if (c == '.')
				is_float = 1;
			*bufptr++ = c;
		}
		// handle scientific notation (e.g., 1e10, 1.5e-3)
		if (c == 'e' || c == 'E')
		{
			is_float = 1;
			*bufptr++ = c;
			c = getch(pSK);
			if (c == '+' || c == '-')
			{
				*bufptr++ = c;
				c = getch(pSK);
			}
			while (isdigit(c))
			{
				*bufptr++ = c;
				c = getch(pSK);
			}
		}
	}
	
	// need to put back the last character
	ungetch(pSK, c);

	// make sure string is asciiz
	*bufptr = '\0';

	if (is_float)
	{
		pSK->yylval.fval = (float)strtod(buf, NULL);
		return FLOAT;
	}
	else
	{
		pSK->yylval.ival = strtol(buf, NULL, base);
		return NUMBER;
	}
}

//-------------------------------
// translate backslash characters
//-------------------------------
int backslash(SKState* pSK, int c)
{
	static char translation_tab[] = "a\ab\bf\fn\nr\rt\te\x1b";

	if (c != '\\')
		return c;

	c = getch(pSK);
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

    c = getch(pSK);

    while (c != delim && cptr < &buf[sizeof(buf)])
    {
        if (c == '\n' || c == EOF)
            yyerror(pSK, "missing end quote");

        *cptr++ = backslash(pSK, c);
        c = getch(pSK);
    }
    
    *cptr = 0;

    // lookup symbol and return if exists
    int sym_num = lookup_symbol(pSK, buf);
    if (sym_num != -1)
    {
        pSK->yylval.symbol = sym_num;
        // LOG("Existing symbol ref'd: '%s' (index = %d)\n", buf, sym_num);
        return STRING;
    }

    sym_num = add_symbol(pSK, buf, pSK->lineno);

    pSK->yylval.symbol = sym_num;
    //strcpy(pSK->yylval.word, buf);
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
    while ((c = getch(pSK)) == ' ' || c == '\t');

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
        skipToEOL(pSK);
        goto yylex01;
    }

    // look for char literals
    if (c == '\'')
    {
        c = getch(pSK);
        if (c == '(' && follow(pSK, ')', TRUE, FALSE))
        {
            return NIL;
        }
        else if (follow(pSK, '\'', TRUE, FALSE))
        {
            pSK->yylval.ival = backslash(pSK, c);
            return CHAR;
        }

        ungetch(pSK, c);
        return getString(pSK, '\'');
    }

    // look for empty lists
    if (c == '(' && follow(pSK, ')', TRUE, FALSE))
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
            int n = getch(pSK);
            ungetch(pSK, n);

            if (n != '$' && !isdigit(n))
            {
                pSK->yylval.word[0] = c; pSK->yylval.word[1] = 0;
                return c;
            }
        }

		ungetch(pSK, c);
		return getNumber(pSK);
	}

    // look for Scheme-style character literals (#\a, #\newline, etc.)
    if (c == '#')
    {
        int next = getch(pSK);
        if (next == '\\')
        {
            // Parse character literal
            c = getch(pSK);
            
            // Check for named characters (newline, space, tab, return)
            if (isalpha(c))
            {
                char buf[16];
                int i = 0;
                buf[i++] = c;
                while (i < 15 && isalpha(c = getch(pSK)))
                    buf[i++] = c;
                buf[i] = '\0';
                ungetch(pSK, c);  // put back non-alpha char
                
                // Check for named characters
                if (i == 1)
                {
                    // Single character like #\a
                    pSK->yylval.ival = buf[0];
                    return CHAR;
                }
                else if (!sk_strcasecmp(buf, "newline"))
                {
                    pSK->yylval.ival = '\n';
                    return CHAR;
                }
                else if (!sk_strcasecmp(buf, "space"))
                {
                    pSK->yylval.ival = ' ';
                    return CHAR;
                }
                else if (!sk_strcasecmp(buf, "tab"))
                {
                    pSK->yylval.ival = '\t';
                    return CHAR;
                }
                else if (!sk_strcasecmp(buf, "return"))
                {
                    pSK->yylval.ival = '\r';
                    return CHAR;
                }
                else
                {
                    yyerror(pSK, "unknown character name");
                    return CHAR;
                }
            }
            else
            {
                // Single character like #\( or #\)
                pSK->yylval.ival = c;
                return CHAR;
            }
        }
        else
        {
            // Not a character literal, put back and continue with # token
            ungetch(pSK, next);
        }
    }

    // look for start of a token (identifiers starting with # like #t, #f)
    if (c == '#' || isalpha(c) || c == '_')
    {
        char buf[BUF_SIZE], *p = buf;

        do {
            *p++ = c;
        } while ((c=getch(pSK)) != EOF && (c == '_' || c == '?' || c == '-' || c == '!' || c == '>' || c == '<' || isalnum(c)));
        
        // put back the last character!
        ungetch(pSK, c);

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
            sym_num = add_symbol(pSK, buf, pSK->lineno);
            // LOG("New symbol: '%s' (index = %d)\n", buf, sym_num);
        }
        else
        {
            // LOG("Existing symbol ref'd: '%s' (index = %d)\n", buf, sym_num);
        }

        pSK->yylval.symbol = sym_num;
        strcpy(pSK->yylval.word, buf);
        return ID;
    }

    // track line numbers
    if (c == '\n')
    {
        pSK->lineno++;           // update global line count
        if (pSK && pSK->file_stack_ptr >= 0)
            CURRENT_LINENO++;   // update file local line number
        
        if (pSK->yyin == stdin)
            return DONE;

        goto yylex01;
    }

    // handle two-character tokens
    switch (c)
    {
    case '<':
        if (follow(pSK, '=', TRUE, FALSE))
        {
			strcpy(pSK->yylval.word, "<=");
            return ID;
        }
        break;

    case '>':
        if (follow(pSK, '=', TRUE, FALSE))
        {
            strcpy(pSK->yylval.word, ">=");
            return ID;
        }
        break;
    }

    // return single character tokens
    pSK->yylval.word[0] = c; pSK->yylval.word[1] = 0;
    return c;
}
