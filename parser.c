#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

#ifdef __linux
    #include <limits.h> // for PATH_MAX
#endif

#ifdef __APPLE__
    #include <sys/syslimits.h> // for PATH_MAX
#endif

#ifdef _WIN32
    #include <direct.h> // for _getcwd
#else
    #include <unistd.h> // for getcwd
#endif

#ifdef _WIN32
    #define PATH_MAX _MAX_PATH
    #define getcwd _getcwd
#endif

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
// parse the input
//-----------------------------------------------
static int yyparse(void)
{
    if (yyin == NULL)
        return 0;

    // prime the pump
    lookahead = yylex();

    // the top-level is a series of s-expressions
    while (lookahead != DONE)
        s_expr();

    return 0;
}

//
//[]-----------------------------------------------
//  built-in functions
//[]-----------------------------------------------
//

//-----------------------------------------------
// print an object
//-----------------------------------------------
static void printObject(Object *obj)
{
    if (!obj)
    {
        printf("'()");   // nil
        return;
    }

    if (IS_BOOL(obj))
    {
        if (IS_TRUE(obj))
            printf("#t");
        else
            printf("#f");

        return;
    }

    if (IS_OBJ(obj))
    {
        switch (obj->type)
        {
        case OBJ_STRING:
            {
                String* strObj = (String*)obj;
                printf("\"%s\"", strObj->value);
            }
            break;

        case OBJ_PAIR:
            {
                Pair* pair = (Pair*)obj;
                printf("(");

                printObject(pair->car); // display car

                Object* cdr = pair->cdr;
                while (cdr && IS_OBJ(cdr) && cdr->type == OBJ_PAIR)
                {
                    pair = (Pair*)cdr;
                    printf(" ");
                    printObject(pair->car); // display car
                    cdr = pair->cdr;
                }

                if (cdr && !IS_NIL(cdr))
                {
                    printf(" . ");
                    printObject(pair->car); // display car
                }
                printf(")\n");
            }
			break;

        case OBJ_NATIVE_FN:
            printf("#<native fn>\n");
			break;

   //     case OBJ_BUILTIN_FN:
			//printf("#<builtin fn>\n");
			//break;

        default:
            printf("#<obj type=%d>\n", obj->type);
            break;
        }
    }
    else
    {
        // print number
        printf("%d", AS_NUM(obj));
    }
}

//-----------------------------------------------
// built-in function: display
//-----------------------------------------------
int sk_display(SKState *pSK)
{
	if (!pSK) return SK_FALSE;

    // pop the top of the stack and print it
    Object *obj = vm_pop(&vm);

    printf(TERM_BRIGHT_BLUE);
        printObject(obj);
    printf(TERM_RESET);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: null?
//-----------------------------------------------
int sk_nullp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (IS_NIL(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: eq?
//-----------------------------------------------
int sk_eqp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (obj1 == obj2)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: number?
//-----------------------------------------------
int sk_numberp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (IS_NUM(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: boolean?
//-----------------------------------------------
int sk_booleanp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (obj && IS_BOOL(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: pair?
//-----------------------------------------------
int sk_pairp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cons
//-----------------------------------------------
int sk_cons(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_peek(&vm, 0);     // need objects to stay live for now in case of GC
    Object* obj1 = vm_peek(&vm, 1);

	Pair* pair = (Pair*)vm_newObject(&vm, sizeof(Pair));    // might GC here!
    pair->base.type = OBJ_PAIR;
    pair->car = obj1;
    pair->cdr = obj2;

    vm_pop(&vm);        // done with obj2
    vm_pop(&vm);        // done with obj1
    vm_push(&vm, (Object*)pair);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: not
//-----------------------------------------------
int sk_not(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (IS_BOOL(obj) && IS_FALSE(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: car
//-----------------------------------------------
int sk_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
        result = ((Pair*)obj)->car;
    else
        yyerror("argument to 'car' is not a pair");

    vm_push(&vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cdr
//-----------------------------------------------
int sk_cdr(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
        result = ((Pair*)obj)->cdr;
    else
        yyerror("argument to 'cdr' is not a pair");

    vm_push(&vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: exit
//-----------------------------------------------
int sk_exit(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    g_bDone = TRUE;
    //exit(0);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: error
//-----------------------------------------------
int sk_error(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    const char* error_msg = "error called";

	// get error message from stack
    Object* obj = vm_pop(&vm);
    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
    {
        // Get the error message from the string object
        error_msg = ((String*)obj)->value;
    }

    // simply raise a parser error for now
    yyerror(error_msg);
    
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: define
//-----------------------------------------------
int sk_define(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement define
    Object* obj_value = vm_peek(&vm, 0);
    const char* key = (const char*)vm_peek(&vm, 1);

    Object *obj_key = vm_newString(&vm, key);

    // Add the new global variable
    vm_define_global(&vm, obj_key, obj_value);

	vm_pop(&vm);
    vm_pop(&vm);        // done with values
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string?
//-----------------------------------------------
int sk_stringp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: list?
//-----------------------------------------------
int sk_listp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    // check for proper list (nil or chain of pairs ending in nil)
    if (obj == NULL)
    {
        bool = MAKE_TRUE();
    }
    else
    {
        Object* current = obj;
        int is_list = FALSE;
        while (current && IS_OBJ(current))
        {
            if (current->type == OBJ_PAIR)
            {
                current = ((Pair*)current)->cdr;
                if (IS_NIL(current))
                {
                    is_list = TRUE;
                    break;  // end of list
                }
            }
            else
            {
                is_list = FALSE;
                break;
            }
        }
        if (is_list)
            bool = MAKE_TRUE();
    }

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// force a gc
//-----------------------------------------------
int sk_gc(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_gc(&vm);

    return SK_TRUE;
}

//-----------------------------------------------
// display gc stats
//-----------------------------------------------
int sk_gc_stats(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, "GC Stats: %d live obj (%zu peak), %zu bytes in use (%zu peak), %zu gc passes, next GC at %zu bytes\n",
        vm.numObjects,
        vm.peakObjects,
        vm.allocatedBytes,
        vm.peakAllocatedBytes,
        vm.gc_cycles,
        vm.nextGC
    );

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: load
//-----------------------------------------------
int sk_load(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement load
	Object* obj = vm_pop(&vm);
    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
    {
        // Get the filename from the string object
        const char* filename = ((String*)obj)->value;
		push_file_stack(filename);
        yyparse();
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: if
//-----------------------------------------------
int sk_if(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement if

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: zero?
//-----------------------------------------------
int sk_zerop(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);
    
    if (obj && IS_NUM(obj) && AS_NUM(obj) == 0)
        bool = MAKE_TRUE();
    
    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
//
//-----------------------------------------------
int sk_less(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj1 = vm_peek(&vm, 0);
    Object* obj2 = vm_peek(&vm, 1);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '<' must be numbers");
        return SK_FALSE;
	}

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    if (AS_NUM(obj1) < AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_pop(&vm);        // done with obj
    vm_pop(&vm);        // done with obj
    vm_push(&vm, bool);

    return SK_TRUE;
}

//-----------------------------------------------
//
//-----------------------------------------------
int sk_strings(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement string concatenation
    for (int i = 0; i < symbol_count; i++)
    {
        // if (symbols[i].type == ST_STRING)
        // {
            printf("String[%d]: '%s'\n", i, symbols[i].name);
        // }
    }
    return SK_TRUE;
}

//-----------------------------------------------
//
//-----------------------------------------------
int sk_pwd(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	char cwd[PATH_MAX];
	if (NULL == getcwd(cwd, sizeof(cwd)))
		yyerror("failed to get current working directory");
    else
        printf("Current working directory: '%s'\n", cwd);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: list
//-----------------------------------------------
int sk_list(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement list creation
    // pop the number of elements from the stack
    // int count = vm_pop(&vm) ? AS_NUM(vm_pop(&vm)) : 0;
    
    // create a linked list of pairs
    // push the list object onto the stack

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: length
//-----------------------------------------------
int sk_length(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement length function for lists and strings

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: set-car!
//-----------------------------------------------
int sk_set_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement set-car!
    Object *obj = vm_peek(&vm, 1);
    if (!obj || !IS_OBJ(obj) || obj->type != OBJ_PAIR)
    {
        vm_pop(&vm); // remove values
        vm_pop(&vm);
        yyerror("argument to 'set-car!' is not a pair");
        return SK_FALSE;
    }

    Pair* pair = (Pair*)obj;
    Object* new_car = vm_pop(&vm); // get new car value
    pair->car = new_car;

    // leave pair on stack

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: set-cdr!
//-----------------------------------------------
int sk_set_cdr(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement set-cdr!

    return SK_TRUE;
}

//-----------------------------------------------
// built-in functions
//-----------------------------------------------
static SKFuncSet built_in_funcs[] =
{
    { "display", sk_display },
    { "null?", sk_nullp },
    { "eq?", sk_eqp },
    { "number?", sk_numberp },
    { "boolean?", sk_booleanp },
    { "pair?", sk_pairp },
    { "cons", sk_cons },
    { "not", sk_not },
    { "car", sk_car },
    { "cdr", sk_cdr },
    { "exit", sk_exit },
	{ "error", sk_error },
    { "define", sk_define },
    { "string?", sk_stringp },
    { "list?", sk_listp },
    { "gc", sk_gc },
    { "gc-stats", sk_gc_stats },
    { "load", sk_load },
    { "zero?", sk_zerop },
    { "strings", sk_strings },
	{ "pwd", sk_pwd },
    { "list", sk_list },
    { "length", sk_length },
    { "set-car!", sk_set_car },
    { "set-cdr!", sk_set_cdr },

    //{ "if", sk_if },
    //{ "<", sk_less, },
    // { "lambda", sk_lambda },
    // { "list", sk_list },
    // { "print", sk_print },
    // { "atom?", sk_atomp },
    // { "char?", sk_charp },
	// { "symbol?", sk_symbolp },
	// { "read", sk_read },
	// { "eval", sk_eval },
	// { "map", sk_map },
	// { "filter", sk_filter },
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
    int result = yyparse();
    vm_emit(&vm, OP_HALT);
    return result;
}

