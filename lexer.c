//---------------------------------------------------------
// lexer.c - implementation of the lexer for SK
//
// Copyright (c) 2025 Mark Seminatore
//----------------------------------------------------------

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
    { "begin",   BEGIN },
    { "case",    CASE_TOKEN },
    { "import",  IMPORT },
    { "let",     LET },
    { "let*",    LET_STAR },
    { "letrec",  LETREC },
    { "quote",   QUOTE },
    { "do",      DO },
    { "when",    WHEN },
    { "unless",  UNLESS },
    { "quasiquote", QUASIQUOTE },
    { "unquote", UNQUOTE },
    { "unquote-splicing", UNQUOTE_SPLICING },
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

//-----------------------------------
// Is the lexer reading the eval string? Not while a file loaded from it
// is on the file stack above it.
//-----------------------------------
int sk_reading_string(SKState* pSK)
{
    return pSK->eval_input && pSK->file_stack_ptr == pSK->eval_file_depth;
}

//-----------------------------------
// Where the lexer is: the eval string (by its name and its own lines), or
// the file on top of the input stack. Returns 0 if neither has a name.
//-----------------------------------
int sk_source_location(SKState* pSK, const char** file, int* line)
{
    if (sk_reading_string(pSK) && pSK->eval_name)
    {
        *file = pSK->eval_name;
        *line = pSK->eval_lineno;
        return 1;
    }

    // a do loop's step code, re-read from a string, names its file
    if (pSK->file_stack && pSK->file_stack_ptr >= 0 && pSK->file_stack[pSK->file_stack_ptr].filename)
    {
        *file = pSK->file_stack[pSK->file_stack_ptr].filename;
        *line = pSK->file_stack[pSK->file_stack_ptr].yylineno;
        return 1;
    }
    return 0;
}

//-----------------------------------
// next next char from input stream
//-----------------------------------
int getch(SKState* pSK)
{
    if (sk_reading_string(pSK)) {
        char c = pSK->eval_input[pSK->eval_pos];
        if (c == '\0') return EOF;
        pSK->eval_pos++;
        return (unsigned char)c;
    }
    return fgetc(pSK->yyin);
}

//-----------------------------------
// put char back to input stream
//-----------------------------------
int ungetch(SKState* pSK, int c)
{
    // getch did not advance past the end, so there is nothing to put back
    // (ungetc ignores EOF too)
    if (c == EOF)
        return c;

    if (sk_reading_string(pSK)) {
        if (pSK->eval_pos > 0) pSK->eval_pos--;
        return c;
    }
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

	if (!strpbrk(buf, base == 16 ? "0123456789abcdefABCDEF" : "0123456789"))
		yyerror(pSK, "malformed number");

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

    while (c != delim)
    {
        if (c == EOF)
        {
            yyerror(pSK, "missing end quote");
            break;
        }
        if (cptr == &buf[sizeof(buf) - 1])
        {
            // too long: report it once, and skip to the closing quote
            yyerror(pSK, "string literal too long");
            while (c != delim && c != EOF)
                c = getch(pSK);
            break;
        }

        // a string may span lines; keep the line count right
        if (c == '\n')
        {
            if (sk_reading_string(pSK))
                pSK->eval_lineno++;
            else
            {
                pSK->lineno++;
                if (pSK->file_stack_ptr >= 0)
                    CURRENT_LINENO++;
            }
        }

        *cptr++ = backslash(pSK, c);
        c = getch(pSK);
    }

    *cptr = 0;

    strncpy(pSK->yylval.word, buf, BUF_SIZE - 1);
    pSK->yylval.word[BUF_SIZE - 1] = '\0';

    return STRING;
}

static int next_token(SKState* pSK);

//------------------------
// lexical analyzer: returns the next token and tracks paren nesting,
// which lets the parser skip the rest of a form after a syntax error
//------------------------
int yylex(SKState* pSK)
{
    int token;
    TokenReplay* replay = pSK->replay;

    if (replay)
    {
        // hand out recorded tokens first (see TokenReplay)
        token = replay->tokens[replay->pos];
        pSK->yylval = replay->values[replay->pos];
        if (++replay->pos == replay->count)
        {
            pSK->replay = replay->next;
            free(replay->tokens);
            free(replay->values);
            free(replay);
        }
    }
    else
        token = next_token(pSK);

    if (token == '(')
        pSK->paren_depth++;
    else if (token == ')')
        pSK->paren_depth--;

    return token;
}

static int next_token(SKState* pSK)
{
    int c;

yylex01:
    // skip leading whitespace
    while ((c = getch(pSK)) == ' ' || c == '\t');

    // see if input stack is empty
    if (c == EOF)
    {
        // the end of the eval string ends the input; the end of a file
        // loaded from it pops back to the string
        if (sk_reading_string(pSK))
            return DONE;

        if (pSK->eval_input && pSK->file_stack_ptr > pSK->eval_file_depth)
        {
            // back to the string, even when that leaves only the sentinel
            // entry, which pop_file_stack reports as the end of input
            pop_file_stack(pSK);
            goto yylex01;
        }

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

    // quasiquote syntax: `datum, ,expr and ,@expr. The word is the
    // abbreviation, as for ' (the long forms are keywords).
    if (c == '`')
    {
        strcpy(pSK->yylval.word, "`");
        return QUASIQUOTE;
    }
    if (c == ',')
    {
        if (follow(pSK, '@', TRUE, FALSE))
        {
            strcpy(pSK->yylval.word, ",@");
            return UNQUOTE_SPLICING;
        }
        strcpy(pSK->yylval.word, ",");
        return UNQUOTE;
    }

    // look for char literals and quote syntax
    if (c == '\'')
    {
        c = getch(pSK);
        if (c == '(' && follow(pSK, ')', TRUE, FALSE))
        {
            return NIL;     // '() is empty list
        }
        else if (follow(pSK, '\'', TRUE, FALSE))
        {
            pSK->yylval.ival = backslash(pSK, c);
            return CHAR;    // 'x' is character literal
        }

        // Quote syntax: '( starts a quoted list, 'identifier quotes a symbol
        ungetch(pSK, c);
        strcpy(pSK->yylval.word, "'");     // marks the apostrophe form, as opposed to the word quote
        return QUOTE;
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
	if (isdigit(c) || c == '-' || c == '+' || c == '$' || c == '.')
	{
        if (c == '-' || c == '+' || c == '.')
        {
            int n = getch(pSK);
            ungetch(pSK, n);

            // a lone '.' is the dotted-pair token; '.5' is a number.
            // '-' or '+' starts a number before a digit, '$' or '.' (e.g. -.5)
            if (!isdigit(n) && (c == '.' || (n != '$' && n != '.')))
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
                else if (!sk_strcasecmp(buf, "escape"))
                {
                    pSK->yylval.ival = '\x1b';
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

        // after the first character, an identifier can contain any R5RS
        // "subsequent" character, e.g. call/cc, string=?, list->string
        do {
            if (p < buf + BUF_SIZE - 1)
                *p++ = c;
        } while ((c=getch(pSK)) != EOF && (isalnum(c) || (c != 0 && strchr("_?-!><*/=+.:%&^~$@", c))));
        
        // put back the last character!
        ungetch(pSK, c);

        // be sure to null terminate the string
        *p = 0;

        // always populate yylval.word with the matched lexeme
        strncpy(pSK->yylval.word, buf, BUF_SIZE - 1);
        pSK->yylval.word[BUF_SIZE - 1] = '\0';

        int token = isToken(buf);
        if (token)
        {
            return token;
        }
        
        return ID;
    }

    // track line numbers
    if (c == '\n')
    {
        // a newline in an eval string counts lines in the string; it
        // neither moves the file's position nor ends a REPL line
        if (sk_reading_string(pSK))
        {
            pSK->eval_lineno++;
            goto yylex01;
        }

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
