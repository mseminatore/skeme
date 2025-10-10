#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"

// parser tracking
int err_count = 0;
int warn_count = 0;

// input and output files
FILE *yyin = NULL;
FILE *fout = NULL;

//------------------------
// error routine
//------------------------
void yyerror(char *s)
{
    fprintf(stderr, "ERROR: '%s' in file '%s' near line %d\n", s, CURRENT_FILE, CURRENT_LINENO);
    err_count++;
}

//------------------------
// error routine
//------------------------
void yywarning(char *s)
{
    fprintf(stderr, "WARNING: '%s' in file '%s' near line %d\n", s, CURRENT_FILE, CURRENT_LINENO);
    warn_count++;
}

//---------------------------------------------------
// return the string representation of a token
//---------------------------------------------------
// const char *getLexemeFromToken(int token)
// {
// 	// look for single char tokens
// 	if (token < 256)
// 		return (const char*)token;

// 	// look for user tokens
//     Tokens *pTokens = tokens;

//     for (; pTokens->lexeme != NULL; pTokens++)
//     {
//         if (token == pTokens->token)
//             return pTokens->lexeme;
//     }

// 	return "(unknown token)";
// }

//---------------------------------------------------
// error message for unexpected tokens
//---------------------------------------------------
void expected(int token)
{
	if (token < 256)
		yyerror("unexpected character"); //'%c'", token);
	else
		yyerror("expected token"); //'%s'", getLexemeFromToken(token));
}

//---------------------------------------------------
// if token is matched continue, otherwise error
//---------------------------------------------------
int match(int token)
{
	if (lookahead == token)
	{
		lookahead = yylex();
	}
	else
	{
		expected(token);
	}

	return lookahead;
}

//------------------------
// symbol comparison fn
//------------------------
int compare_fn(const void *a, const void *b)
{
    Symbol_t *s1 = (Symbol_t*)a;
    Symbol_t *s2 = (Symbol_t*)b;

    return strcasecmp(s1->name, s2->name);
}

//------------------------
// print out symbols
//------------------------
void dump_symbols()
{
    // alpha sort symbols
    qsort(symbols, symbol_count, sizeof(Symbol_t), compare_fn);

    puts("\nSymbol table");
    for (int i = 0; i < symbol_count; i++)
    {
        if (symbols[i].type == ST_LABEL)
            printf("%10s\t%04X\n", symbols[i].name, symbols[i].value);
    }
}

//-----------------------------------------------
// parse a single s-expression
//-----------------------------------------------
void s_expr()
{
    switch(lookahead)
    {
        case DONE:
            return;

        case ')':
            yyerror("unexpected ')'");
            match(')');
            return;

        case '(':
            match('(');
            while (lookahead != ')')
            {
                s_expr();
            }
            match(')');
            break;

        case DEFINE:
            match(DEFINE);
            break;

        case LAMBDA:
            match(LAMBDA);
            break;

        case ID:
            // TODO - lookup symbol
            LOG("ID: '%s'\n", symbols[yylval.symbol].name);
            // int sym_num = lookup_symbol(symbols[yylval.symbol].name);
            // if (sym_num == -1)
            // {
            //     sym_num = add_symbol(symbols[yylval.symbol].name, lineno);
            //     if (sym_num == -1)
            //         yyerror("symbol already defined");
            // }
            // yylval.symbol = sym_num;
            // LOG("Symbol index = %d\n", sym_num);
            match(ID);
            break;

        case NUMBER:
            // TODO - handle number
            LOG("NUMBER: %d\n", yylval.ival);
            match(NUMBER);
            break;

        default:
            yyerror("expected s-expression");
            match(lookahead); // try to continue
            return;
    }
}

//-----------------------------------------------
//
//-----------------------------------------------
int yyparse(void)
{
    lookahead = yylex();

    // TODO - implement parser here
    while (lookahead != DONE)
        s_expr();

    return 0;
}
