#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

// TODO - move inside SKState
VM vm;

// parser tracking
int err_count = 0;
int warn_count = 0;

// input and output files
FILE *yyin = NULL;
FILE *fout = NULL;
extern int g_bDone;

// forward declarations
int s_expr();
int list();
int sk_init_builtins(SKState* pSK);

//------------------------
// error routine
//------------------------
void yyerror(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_RED"ERROR: '%s'\n"TERM_RESET, s);
    else
        fprintf(stderr, TERM_RED"ERROR: '%s' in file '%s' near line %d\n"TERM_RESET, s, CURRENT_FILE, CURRENT_LINENO);
    err_count++;
}

//------------------------
// error routine
//------------------------
void yywarning(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_YELLOW"WARNING: '%s'\n"TERM_RESET, s);
    else
        fprintf(stderr, TERM_YELLOW"WARNING: '%s' in file '%s' near line %d\n"TERM_RESET, s, CURRENT_FILE, CURRENT_LINENO);
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

    puts("\nSymbol table\n------------");
    for (int i = 0; i < symbol_count; i++)
    {
        // if (symbols[i].type == ST_LABEL)
            printf("%10s\t%04X\n", symbols[i].name, symbols[i].value);
    }
}

//-----------------------------------------------
// parse an atomic value
//-----------------------------------------------
int atom()
{
    Symbol_t sym;

    switch (lookahead)
    {
    case '(':
        list();
        return 1;

    case TOKEN_TRUE:
        vm_emit(&vm, OP_TRUE);
        match(TOKEN_TRUE);
        return 1;

    case TOKEN_FALSE:
        vm_emit(&vm, OP_FALSE);
        match(TOKEN_FALSE);
        return 1;

    case NUMBER:
        // handle number
        LOG("NUMBER: %d\n", yylval.ival);
        vm_emit(&vm, OP_NUM);
        vm_emit(&vm, yylval.ival);
        match(NUMBER);
        return 1;

    case STRING:
        // handle string
        LOG("STRING: '%s'\n", symbols[yylval.symbol].name);
		vm_emit(&vm, OP_STRING);
		vm_emit(&vm, (VMCode_t)symbols[yylval.symbol].name);
        match(STRING);
        return 1;

    case NIL:
        LOG("NIL\n");
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)NULL);
        match(NIL);
        return 1;

    case ID:
        // lookup symbol
        sym = symbols[yylval.symbol];
        LOG("ID: '%s'\n", sym.name);

        // undefined symbol error
        if (sym.type == ST_UNDEF)
        {
            //yywarning("symbol is undefined");
            match(ID);
			vm_emit(&vm, OP_PUSH);
			vm_emit(&vm, (VMCode_t)sym.name);
		}

        // handle native function call
        if (sym.type == ST_NATIVE)
        {
			sym = symbols[yylval.symbol];
            match(ID);
            s_expr(); // parse function arguments (if any)
			vm_emit(&vm, OP_NATIVE);
			vm_emit(&vm, (VMCode_t)sym.func);
        }

        return 1;
    }

    return 0;
}

//-----------------------------------------------
// parse multiple atomic values
//-----------------------------------------------
int atoms()
{
    int count = 0;

    while (atom())
        count++;

    return count;
}

//-----------------------------------------------
// parse a list
//-----------------------------------------------
int list()
{
    match('(');

    while (lookahead != ')')
    {
        s_expr();
    }

    match(')');

    return 1;
}

//-----------------------------------------------
// parse a single s-expression
//-----------------------------------------------
int s_expr()
{
    if (lookahead == '(')
        return list();
    else
        return atoms();
}

//-----------------------------------------------
//
//-----------------------------------------------
int command()
{
    Symbol_t sym;

    sym = symbols[yylval.symbol];
    if (sym.type == ST_NATIVE)
    {
        match(ID);
        s_expr(); // parse function arguments (if any)
        vm_emit(&vm, OP_NATIVE);
        vm_emit(&vm, (VMCode_t)sym.func);
    }
    else
    {
        // handle variable
        Object* key = vm_newString(&vm, yylval.word);

        match(ID);
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)key);
        vm_emit(&vm, OP_GET_VAR);
    }

	return 0;
}

//-----------------------------------------------
// defintion := '(' 'define' ( function-definition | variable-definition ) ')'
// function-definition := '(' ID parameter* ')' s-expr
// variable-definition := ID s-expr
//-----------------------------------------------
int definition()
{
    Symbol_t sym;

    match(DEFINE);

    // look for a function definition or variable definition
    if (lookahead == '(')
    {
		// function definition
		match('(');
        
        // function name
        match(ID);
		
        // parameters
        while (lookahead == ID)
			match(ID);
		match(')');

		s_expr(); // function body
	}
    else
    {
        sym = symbols[yylval.symbol];

        Object* key = vm_newString(&vm, sym.name);
        vm_define_global(&vm, key, NULL);
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)key);

        match(ID);
        s_expr();
        vm_emit(&vm, OP_ASSIGN);
    }

    return 0;
}

//-----------------------------------------------
// lambda := '(' 'lambda' ( lambda-definition ) ')'
// lambda-definition := '(' parameter* ')' s-expr
//-----------------------------------------------
int lambda()
{
    match(LAMBDA);

    return 0;
}

//-----------------------------------------------
// program := <commmand | definition>*
//-----------------------------------------------
int expr()
{
    Symbol_t sym;

    while (lookahead != DONE)
    {
#if 1
        if (lookahead == '(')
        {
            match('(');

            switch (lookahead)
            {
            case DEFINE:
                definition();
                break;

            case LAMBDA:
                lambda();
                break;
            
            default:
                // handle native function call
                sym = symbols[yylval.symbol];
                if (sym.type == ST_NATIVE)
                {
                    match(ID);
                    s_expr(); // parse function arguments (if any)
                    vm_emit(&vm, OP_NATIVE);
                    vm_emit(&vm, (VMCode_t)sym.func);
                }
                else
                    yyerror("procedure expected!");

                break;
            }

            match(')');
        }
        else
            command();
#else
        s_expr();
#endif
    }

    return 0;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(void)
{
    if (yyin == NULL)
        return 0;

    // prime the pump
    lookahead = yylex();

    // the top-level is zero or more s-expressions
    expr();

    return 0;
}

//-----------------------------------------------
// register built-in functions
//-----------------------------------------------
int sk_register_functions(SKState *pSK, const SKFuncSet funcs[])
{
	for (int i = 0; funcs[i].name && funcs[i].func; i++)
	{
        int sym_num = add_symbol(funcs[i].name, 0);
		if (sym_num == -1)
			yyerror("symbol already defined");

		symbols[sym_num].type = ST_NATIVE;
        symbols[sym_num].func = funcs[i].func;
		LOG("Registered built-in function '%s' (index = %d)\n", funcs[i].name, sym_num);
	}

    return 0;
}

//-----------------------------------------------
// init skeme
//-----------------------------------------------
int sk_init(SKState *pSK)
{
    // initialize the VM
    if (VM_OK != vm_init(&vm))
    {
        fprintf(stderr, "Failed to initialize VM!\n");
        return FALSE;
    }

    // register built-in functions
    if (SK_TRUE != sk_init_builtins(pSK))
    {
        fprintf(stderr, "Failed to register built-in functions!\n");
        return FALSE;
    }

    return TRUE;
}

//-----------------------------------------------
// parse the input
//-----------------------------------------------
int sk_parse()
{
    int result = yyparse();
    vm_emit(&vm, OP_HALT);
    return result;
}

