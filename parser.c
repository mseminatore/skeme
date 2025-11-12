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
int g_bParseLog = FALSE;

// forward declarations
int s_expr();
int list();
int sk_init_builtins(SKState* pSK);

#define PARSE_LOG(...) (void)(g_bParseLog && fprintf(stderr, __VA_ARGS__))

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
// warning routine
//------------------------
void yywarning(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_YELLOW"WARNING: '%s'\n"TERM_RESET, s);
    else
        fprintf(stderr, TERM_YELLOW"WARNING: '%s' in file '%s' near line %d\n"TERM_RESET, s, CURRENT_FILE, CURRENT_LINENO);
    warn_count++;
}

//------------------------
// info routine
//------------------------
void yyinfo(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_GREEN"INFO: '%s'\n"TERM_RESET, s);
    else
        fprintf(stderr, TERM_GREEN"INFO: '%s' in file '%s' near line %d\n"TERM_RESET, s, CURRENT_FILE, CURRENT_LINENO);
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
static int compare_fn(const void *a, const void *b)
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
    PARSE_LOG("atom(): lookahead = %d\n", lookahead);

    //Symbol_t sym;

    switch (lookahead)
    {
    case '(':
        list();
        return 1;

    case TOKEN_TRUE:
        PARSE_LOG("TOKEN_TRUE\n");

        vm_emit(&vm, OP_TRUE);
        match(TOKEN_TRUE);
        return 1;

    case TOKEN_FALSE:
        PARSE_LOG("TOKEN_FALSE\n");

        vm_emit(&vm, OP_FALSE);
        match(TOKEN_FALSE);
        return 1;

    case NUMBER:
        // handle number
        PARSE_LOG("NUMBER: %d\n", yylval.ival);

        vm_emit(&vm, OP_NUM);
        vm_emit(&vm, yylval.ival);
        match(NUMBER);
        return 1;

    case STRING:
        // handle string
        PARSE_LOG("STRING: '%s'\n", symbols[yylval.symbol].name);

		vm_emit(&vm, OP_STRING);
		vm_emit(&vm, (VMCode_t)symbols[yylval.symbol].name);
        match(STRING);
        return 1;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)NULL);
        match(NIL);
        return 1;

    case ID:
#if 1
    {
        PARSE_LOG("ID: '%s'\n", yylval.word);

        // handle variable and check for undefined symbol
        Object* key = vm_get_string(&vm, yylval.word);
        match(ID);
        if (key)
        {
            vm_emit(&vm, OP_PUSH);
            vm_emit(&vm, (VMCode_t)key);
            //vm_emit(&vm, OP_LOAD);
        }
        else
            yyerror("entity undefined");
    }
#else
        // lookup symbol
        sym = symbols[yylval.symbol];
        PARSE_LOG("ID: '%s'\n", sym.name);

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
#endif
        return 1;
    }

    return 0;
}

//-----------------------------------------------
// parse multiple atomic values
//-----------------------------------------------
int atoms()
{
    PARSE_LOG("atoms(): lookahead = %d\n", lookahead);

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
    PARSE_LOG("list(): lookahead = %d\n", lookahead);

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
    PARSE_LOG("s_expr(): lookahead = %d\n", lookahead);

    if (lookahead == '(')
        return list();
    else
        return atoms();
}

int expr();

//-----------------------------------------------
//
//-----------------------------------------------
int command()
{
    PARSE_LOG("command(): lookahead = %d\n", lookahead);

    switch (lookahead)
    {
    case '(':
        expr();
        return 1;

    case TOKEN_TRUE:
        PARSE_LOG("TOKEN_TRUE\n");

        vm_emit(&vm, OP_TRUE);
        match(TOKEN_TRUE);
        return 1;

    case TOKEN_FALSE:
        PARSE_LOG("TOKEN_FALSE\n");

        vm_emit(&vm, OP_FALSE);
        match(TOKEN_FALSE);
        return 1;

    case NUMBER:
        // handle number
        PARSE_LOG("NUMBER: %d\n", yylval.ival);

        vm_emit(&vm, OP_NUM);
        vm_emit(&vm, yylval.ival);
        match(NUMBER);
        return 1;

    case STRING:
        // handle string
        PARSE_LOG("STRING: '%s'\n", symbols[yylval.symbol].name);

        vm_emit(&vm, OP_STRING);
        vm_emit(&vm, (VMCode_t)symbols[yylval.symbol].name);
        match(STRING);
        return 1;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)NULL);
        match(NIL);
        return 1;

    case ID:
    {
        PARSE_LOG("ID: '%s'\n", yylval.word);

        // handle variable and check for undefined symbol
        Object* key = vm_get_string(&vm, yylval.word);
        match(ID);
        if (key)
        {
            vm_emit(&vm, OP_PUSH);
            vm_emit(&vm, (VMCode_t)key);
            vm_emit(&vm, OP_LOAD);
        }
        else
            yyerror("entity undefined");
    }
    }

	return 0;
}

//-----------------------------------------------
//  <formals> ==> (<variable>*) | <variable>
//-----------------------------------------------
int formals()
{
    return 0;
}

//-----------------------------------------------
//  <body> ==> <definition>* <sequence>
//  <sequence> ==> <command>* <expression>
//  <command> ==> <expression>
//-----------------------------------------------
int local_body()
{
    s_expr();
    return 0;
}

//-----------------------------------------------
// defintion := '(' 'define' ( function-definition | variable-definition ) ')'
// function-definition := '(' ID parameter* ')' s-expr
// variable-definition := ID s-expr
//-----------------------------------------------
int definition()
{
    PARSE_LOG("definition(): lookahead = %d\n", lookahead);

    match(DEFINE);

    // look for a function definition or variable definition
    if (lookahead == '(')
    {
		// function definition
		match('(');
        
        // function name
        Object* proc_name = vm_new_string(&vm, yylval.word);
        Object* proc_obj = vm_new_proc(&vm);
        Procedure* proc = AS_OBJ(Procedure, proc_obj);
        if (proc)
            proc->addr = vm.vm_code_ptr; // save address of function code

        vm_define_global(&vm, proc_name, proc_obj);
        match(ID);
		
        // parameters
        int arity = 0;
        while (lookahead == ID)
        {
			match(ID);
			arity++;
		}
        if (proc)
            proc->arity = arity;

        // TODO - emit code to create stack frame and store parameters

        match(')');
        
        // function body
        local_body();

        // return from function
        vm_emit(&vm, OP_RET);
	}
    else
    {
        Object* key = vm_new_string(&vm, yylval.word);
        vm_define_global(&vm, key, NULL);
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)key);

        match(ID);
        s_expr();
        vm_emit(&vm, OP_STORE);
    }

    return 0;
}

//-----------------------------------------------
// lambda := '(' 'lambda' ( lambda-definition ) ')'
// lambda-definition := '(' parameter* ')' s-expr
//-----------------------------------------------
int lambda()
{
    PARSE_LOG("lambda(): lookahead = %d\n", lookahead);

    match(LAMBDA);

    return 0;
}

//-----------------------------------------------
//
//-----------------------------------------------
int parse_list()
{
    PARSE_LOG("parse_list(): lookahead = %d\n", lookahead);

    match(LIST);
    match('(');
    while (lookahead != ')')
    {
        expr();
    }
    match(')');

    return 0;
}

//-----------------------------------------------
// program := <commmand | definition>*
//-----------------------------------------------
int expr()
{
    PARSE_LOG("expr(): lookahead = %d\n", lookahead);

    //Symbol_t sym;

    while (lookahead != DONE)
    {
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
            
            case LIST:
                parse_list();
                break;

            default:
                {
                    // handle native and regular function calls
                    // handle variable and check for undefined symbol
                    Object* key = vm_get_string(&vm, yylval.word);
                    match(ID);
                    if (key)
                    {
                        Object *proc = vm_get_global(&vm, key);
                        if (proc && proc->type == OBJ_FUNCTION)
						{
							expr(); // parse function arguments (if any)
							vm_emit(&vm, OP_CALL);
							vm_emit(&vm, (VMCode_t)((Procedure*)proc)->addr);
						}
						else if (proc && proc->type == OBJ_NATIVE_FN)
						{
							expr(); // parse function arguments (if any)
							vm_emit(&vm, OP_NATIVE);
							vm_emit(&vm, (VMCode_t)((NativeFunction*)proc)->func);
						}
						else
						{
							yyerror("procedure expected!");
						}
                    }
                    else
                        yyerror("entity undefined");
                }
                break;
            }

            match(')');
        }
        else
            command();

        if (lookahead == ')')
            break;
    }

    return 0;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(void)
{
    PARSE_LOG("yyparse(): lookahead = %d\n", lookahead);

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
    // register with globals + string intern table
	for (int i = 0; funcs[i].name && funcs[i].func; i++)
	{
        Object* key = vm_new_string(&vm, funcs[i].name);
        vm_push(&vm, key);  // for GC safety

        Object* native_fn = vm_newObject(&vm, sizeof(NativeFunction), OBJ_NATIVE_FN);
        NativeFunction* nf = AS_OBJ(NativeFunction, native_fn);
        if (!nf)
        {
            yyerror("failed to alloc object");
            vm_pop(&vm);    // remove key
            return SK_FALSE;
        }

        nf->func = funcs[i].func;
        nf->arity = -1; // TODO - set arity if needed

        vm_define_global(&vm, vm_pop(&vm), native_fn);

        LOG("Registered built-in function '%s'\n", funcs[i].name);
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

