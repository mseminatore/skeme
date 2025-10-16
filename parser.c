#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"

// TODO - move inside SKState
VM vm;

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

    puts("\nSymbol table\n------------");
    for (int i = 0; i < symbol_count; i++)
    {
        // if (symbols[i].type == ST_LABEL)
            printf("%10s\t%04X\n", symbols[i].name, symbols[i].value);
    }
}

//-----------------------------------------------
// parse a single s-expression
//-----------------------------------------------
int s_expr()
{
    Symbol_t sym;
    int elements = 0;

    switch(lookahead)
    {
        case DONE:
            return elements; // done parsing

        case ')':
            yyerror("unexpected ')'");
            match(')');
            return elements;

        case '(':
            match('(');
            while (lookahead != ')')
            {
                s_expr();
            }
            match(')');
            break;

        case NIL:
            vm_emit(&vm, OP_PUSH);
            vm_emit(&vm, NULL);
            match(NIL);
            break;

        case ID:
            // lookup symbol
            sym = symbols[yylval.symbol];
            LOG("ID: '%s'\n", sym.name);

            if (sym.type == ST_UNDEF)
            {
                yywarning("symbol is undefined");
                match(ID);
			}

            if (sym.type == ST_NATIVE)
            {
				sym = symbols[yylval.symbol];
                match(ID);
                s_expr(); // parse function arguments (if any)
				vm_emit(&vm, OP_NATIVE);
				vm_emit(&vm, (VMCode_t)sym.func);
            }

            elements++;
            break;

        case TOKEN_TRUE:
            vm_emit(&vm, OP_TRUE);
            match(TOKEN_TRUE);
            elements++;
            break;

        case TOKEN_FALSE:
            vm_emit(&vm, OP_FALSE);
            match(TOKEN_FALSE);
            elements++;
            break;

        case NUMBER:
            // handle number
            LOG("NUMBER: %d\n", yylval.ival);
            vm_emit(&vm, OP_NUM);
            vm_emit(&vm, yylval.ival);
            match(NUMBER);
            elements++;
            break;

        case STRING:
            // TODO - handle string
            LOG("STRING: '%s'\n", yylval.lexeme);
            match(STRING);
            elements++;
            break;

        default:
            yyerror("expected a valid s-expression");
            match(lookahead); // try to continue
            break;
    }

    return elements;
}

//-----------------------------------------------
// parse the input
//-----------------------------------------------
int yyparse(void)
{
    // prime the pump
    lookahead = yylex();

    // the top-level is a series of s-expressions
    while (lookahead != DONE)
        s_expr();

    vm_emit(&vm, OP_HALT);
    return 0;
}

//-----------------------------------------------
// built-in function: display
//-----------------------------------------------
int sk_display(SKState *pSK)
{
	if (!pSK) return FALSE;

    // pop the top of the stack and print it
    Object *obj = vm_pop(&vm);

    if (!obj)
    {
        puts("()\n");   // nil
        return TRUE;
    }

    if (IS_OBJ(obj))
    {
        switch (obj->type)
        {
        case OBJ_BOOL:
            if (((Boolean*)obj)->value == 0)
                puts("#f");
            else
                puts("#t");

            break;

        case OBJ_NATIVE_FN:
            printf("#<native fn>\n");
			break;

        case OBJ_BUILTIN_FN:
			printf("#<builtin fn>\n");
			break;

        default:
            printf("#<obj type=%d>\n", obj->type);
            break;
        }
    }
    else
    {
        printf("%d\n", AS_NUM(obj));
    }

    return TRUE;
}

//-----------------------------------------------
// built-in function: null?
//-----------------------------------------------
int sk_nullp(SKState *pSK)
{
    if (!pSK) return FALSE;

    Object* obj = vm_pop(&vm);
    Object* bool = vm_newBoolean(&vm, 0);

    if (IS_NIL(obj))
        ((Boolean*)bool)->value = 1;

    vm_push(&vm, bool);

    return TRUE;
}

//-----------------------------------------------
// built-in function: eq?
//-----------------------------------------------
int sk_eqp(SKState *pSK)
{
    if (!pSK) return FALSE;

    Object* obj1 = vm_pop(&vm);
    Object* obj2 = vm_pop(&vm);
    Object* bool = vm_newBoolean(&vm, 0);

    if (obj1 == obj2)
        ((Boolean*)bool)->value = 1;

    vm_push(&vm, bool);

    return TRUE;
}

//-----------------------------------------------
// built-in function: number?
//-----------------------------------------------
int sk_numberp(SKState *pSK)
{
    if (!pSK) return FALSE;

    Object* obj = vm_pop(&vm);
    Object* bool = vm_newBoolean(&vm, 0);

    if (IS_NUM(obj))
        ((Boolean*)bool)->value = 1;

    vm_push(&vm, bool);

    return TRUE;
}

//-----------------------------------------------
// built-in function: boolean?
//-----------------------------------------------
int sk_booleanp(SKState* pSK)
{
    if (!pSK) return FALSE;
    
    Object* obj = vm_pop(&vm);
    Object* bool = vm_newBoolean(&vm, 0);

    if (obj && IS_OBJ(obj) && obj->type == OBJ_BOOL)
        ((Boolean*)bool)->value = 1;

    vm_push(&vm, bool);
    return TRUE;
}

//-----------------------------------------------
// built-in functions
//-----------------------------------------------
SKFuncSet built_in_funcs[] =
{
    { "display", sk_display },
    { "null?", sk_nullp },
    { "eq?", sk_eqp },
    { "number?", sk_numberp },
    { "boolean?", sk_booleanp },
    //{ "pair?", NULL },
    //{ "define", NULL },
    //{ "lambda", NULL },
    //{ "print", NULL },
    //{ "car", NULL },
    //{ "cdr", NULL },
    //{ "cons", NULL },
    //{ "list", NULL },
    // { "atom?", NULL },
    // { "string?", NULL },
    // { "char?", NULL },
    // { "not", NULL },
    // { "exit", NULL },
 
    { NULL, NULL }
};

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
    sk_register_functions(pSK, built_in_funcs);

    return TRUE;
}

//-----------------------------------------------
// parse the input
//-----------------------------------------------
int sk_parse()
{
    return yyparse();
}

