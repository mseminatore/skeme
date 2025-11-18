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
static int indent_level = 0;

// input and output files
FILE *yyin = NULL;
FILE *fout = NULL;
extern int g_bDone;
int g_bParseLog = FALSE;

// forward declarations
static int expr();
int sk_init_builtins(SKState* pSK);

#define PARSE_LOG(...) (void)(g_bParseLog && indent() && fprintf(stderr, __VA_ARGS__))

//------------------------
// pretty print parse tree
//------------------------
static int indent()
{
    for (int i = 0; i < indent_level; i++)
    {
        fprintf(stderr, "  ");
    }
    
	fprintf(stderr, "|- ");
    indent_level++;
    return 1;
}

//------------------------
// parse tree indent
//------------------------
static void unindent() 
{ 
    if (!g_bParseLog) return;

    indent_level--;
    if (indent_level < 0)
        indent_level = 0;
}

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
// error message for unexpected tokens
//---------------------------------------------------
static void expected(int token)
{
	if (token < 256)
		yyerror("unexpected character"); //'%c'", token);
	else
		yyerror("expected token"); //'%s'", getLexemeFromToken(token));
}

//---------------------------------------------------
// if token is matched continue, otherwise error
//---------------------------------------------------
static int match(int token)
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
        printf("%10s\t%04X\n", symbols[i].name, symbols[i].value);
    }
}

//-----------------------------------------------
// <atom> ==> <number> | <string> | <boolean> | 
// <nil> | <identifier> | '(' <expr> ')'
//-----------------------------------------------
static int atom()
{
    PARSE_LOG("atom(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    switch (lookahead)
    {
    case '(':
        expr();
        break;

    case TOKEN_TRUE:
        PARSE_LOG("TOKEN_TRUE\n");

        vm_emit(&vm, OP_TRUE);
        match(TOKEN_TRUE);
        unindent();
        break;

    case TOKEN_FALSE:
        PARSE_LOG("TOKEN_FALSE\n");

        vm_emit(&vm, OP_FALSE);
        match(TOKEN_FALSE);
        unindent();
        break;

    case NUMBER:
        // handle number
        PARSE_LOG("NUMBER: %d\n", yylval.ival);

        vm_emit(&vm, OP_NUM);
        vm_emit(&vm, yylval.ival);
        match(NUMBER);
        unindent();
        break;

    case STRING:
        // handle string
        PARSE_LOG("STRING: '%s'\n", symbols[yylval.symbol].name);

        vm_emit(&vm, OP_STRING);
        vm_emit(&vm, (VMCode_t)symbols[yylval.symbol].name);
        match(STRING);
        unindent();
        break;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)NULL);
        match(NIL);
        unindent();
        break;

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

            unindent();
        }
        break;
    }

    unindent();
    return 0;
}

//-----------------------------------------------
//  <formals> ==> (<variable>*) | <variable>
//-----------------------------------------------
static int formals()
{
    return 0;
}

//-----------------------------------------------
//  <body> ==> <definition>* <sequence>
//  <sequence> ==> <command>* <expression>
//  <command> ==> <expression>
//-----------------------------------------------
static int local_body()
{
    expr();
    return 0;
}

//-----------------------------------------------
// defintion := '(' 'define' ( function-definition | variable-definition ) ')'
// function-definition := '(' ID parameter* ')' s-expr
// variable-definition := ID s-expr
//-----------------------------------------------
static int definition()
{
    PARSE_LOG("definition(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

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
		// variable definition
        Object* key = vm_new_string(&vm, yylval.word);
        vm_define_global(&vm, key, NULL);
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)key);

        match(ID);
        expr();
        vm_emit(&vm, OP_STORE);
    }

    unindent();
    return 0;
}

//-----------------------------------------------
// lambda := '(' 'lambda' ( lambda-definition ) ')'
// lambda-definition := '(' parameter* ')' s-expr
//-----------------------------------------------
static int lambda()
{
    PARSE_LOG("lambda(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(LAMBDA);

    unindent();
    return 0;
}

//-----------------------------------------------
// list := '(' expr* ')'
//-----------------------------------------------
static int parse_list()
{
    PARSE_LOG("parse_list(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    int nargs = 0;

    match(LIST);

    while (lookahead != ')')
    {
        expr();
        nargs++;
    }

    vm_emit(&vm, OP_NUM);
    vm_emit(&vm, nargs);
    vm_emit(&vm, OP_LIST);

    unindent();
    return 0;
}

//-----------------------------------------------
// set := '(' 'set!' ID expr ')'
//-----------------------------------------------
static int set()
{
    PARSE_LOG("set(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(SET);
    // handle variable and check for undefined symbol
    Object* key = vm_get_string(&vm, yylval.word);
    if (!key)
    {
        yyerror("entity undefined");
        unindent();
        return 0;
    }

    match(ID);

    if (key)
    {
        vm_emit(&vm, OP_PUSH);
        vm_emit(&vm, (VMCode_t)key);
        expr();
        vm_emit(&vm, OP_STORE);
    }
    else
        yyerror("entity undefined");
    
    unindent();
    return 0;
}

//-----------------------------------------------
// if_expr := '(' 'if' expr expr expr ')'
//-----------------------------------------------
static int if_expr()
{
    PARSE_LOG("if_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(IF);

    expr();                                     // condition
	vm_emit(&vm, OP_BNT);                       // branch if not true
	VMCode_t else_addr = vm.vm_code_ptr++;      // reserve space for else clause address

    expr();                                     // then branch expression
	vm_emit(&vm, OP_GOTO);                      // jump to endif
    VMCode_t endif_addr = vm.vm_code_ptr++;     // reserve space for endif address
    vm.vm_code[else_addr] = vm.vm_code_ptr;     // backpatch BNT address

#ifdef IF_ELSE_REQUIRED
    expr();                                     // else branch expression
#else
    if (lookahead != ')')
    {
        expr();                                 // else branch expression
    }
#endif

    vm.vm_code[endif_addr] = vm.vm_code_ptr;    // backpatch GOTO endif address

    unindent();
    return 0;
}

//-----------------------------------------------
// cond_expr := '(' 'cond' ( expr expr )* ')'
//-----------------------------------------------
static int cond_expr()
{
    PARSE_LOG("cond_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(COND);

    while (lookahead != ')')
    {
        match('(');

        expr(); // test/condition
        vm_emit(&vm, OP_BNT);                       // branch if not true
		VMCode_t next_addr = vm.vm_code_ptr++;      // reserve space for next clause address

        expr(); // action/consequence

        // return last expr value
		vm_emit(&vm, OP_RET);  // return from function

        match(')');

        vm.vm_code[next_addr] = vm.vm_code_ptr;     // backpatch BNT address
    }

    unindent();
    return 0;
}

//-----------------------------------------------
// expr_tail := expr*
//-----------------------------------------------
static int expr_tail()
{
    int count = 0;

    while (lookahead != DONE && lookahead != ')')
    {
        expr();
        count++;
    }

	// push number of arguments onto stack
    vm_emit(&vm, OP_NUM);
    vm_emit(&vm, count);

    return count;
}

//-----------------------------------------------
// and_expr := '(' 'and' expr* ')'
//-----------------------------------------------
static int and_expr()
{
    PARSE_LOG("and_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);
    int addr_list[MAX_REL_CLAUSES];

    int terms = 0;

    match(AND);

    vm_emit(&vm, OP_PUSH);   // push a dummy nil value onto the stack
    vm_emit(&vm, (VMCode_t)NULL);

    while (lookahead != DONE && lookahead != ')')
    {
        vm_emit(&vm, OP_POP);       // pop the duplicated value
        
        expr();

        vm_emit(&vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&vm, OP_BNT);       // if any term is false, branch to end

        addr_list[terms++] = vm.vm_code_ptr++;      // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror("too many terms in 'and' expression");
            break;
		}
    }

	// return last expr value
	vm_emit(&vm, OP_RET);  // return from function

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm.vm_code[addr_list[i]] = vm.vm_code_ptr;
    }

    vm_emit(&vm, OP_POP);  // pop the duplicated value
    vm_emit(&vm, OP_FALSE);
    return 0;
}

//-----------------------------------------------
// or_expr := '(' 'or' expr* ')'
//-----------------------------------------------
static int or_expr()
{
    PARSE_LOG("or_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);
    int addr_list[MAX_REL_CLAUSES];

    int terms = 0;

    match(OR);

    vm_emit(&vm, OP_PUSH);   // push a dummy nil value onto the stack
    vm_emit(&vm, (VMCode_t)NULL);

    while (lookahead != DONE && lookahead != ')')
    {
        vm_emit(&vm, OP_POP);       // pop the duplicated value
        
        expr();
        
        vm_emit(&vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&vm, OP_BNF);       // if any term is not false, branch to end

        addr_list[terms++] = vm.vm_code_ptr++;  // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror("too many terms in 'or' expression");
            break;
        }
    }

	vm_emit(&vm, OP_FALSE);
	vm_emit(&vm, OP_RET);      // return from function

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm.vm_code[addr_list[i]] = vm.vm_code_ptr;
    }

    // return last expr value
    return 0;
}

//-----------------------------------------------
// program := <commmand | definition>*
//-----------------------------------------------
static int expr()
{
    PARSE_LOG("expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

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

        case SET:
            set();
            break;

        case IF:
            if_expr();
            break;

        case AND:
            and_expr();
            break;

        case OR:
            or_expr();
            break;

        case COND:
            cond_expr();
			break;

        case '+':
        case '-':
        case '*':
        case '/':
        case '<':
        case '>':
        case '=':
        case ID:
            {
                // handle native and regular function calls
                // handle variable and check for undefined symbol
                Object* key = vm_get_string(&vm, yylval.word);
                match(lookahead);
                if (key)
                {
                    Object *proc = vm_get_global(&vm, key);
                    if (proc && proc->type == OBJ_FUNCTION)
                    {
                        expr_tail(); // parse function arguments (if any)

                        vm_emit(&vm, OP_CALL);
                        vm_emit(&vm, (VMCode_t)((Procedure*)proc)->addr);
                    }
                    else if (proc && proc->type == OBJ_NATIVE_FN)
                    {
                        expr_tail(); // parse function arguments (if any)

                        vm_emit(&vm, OP_NATIVE);
                        vm_emit(&vm, (VMCode_t)proc);
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
        atom();

    unindent();
    return 0;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(void)
{
    //PARSE_LOG("yyparse(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    if (yyin == NULL)
        return 0;

    // prime the pump
    lookahead = yylex();

    indent_level = 0;

    // the top-level is zero or more s-expressions
	while (!g_bDone && lookahead != DONE)
    {
        expr();
    }

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
        nf->arity = funcs[i].arity;
    
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

