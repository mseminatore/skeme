#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

// parser tracking
static int indent_level = 0;
static SKState* g_current_sk = NULL;  // current parser state

// input and output files
FILE *yyin = NULL;
FILE *fout = NULL;
extern int g_bDone;

// forward declarations
static int expr();
int sk_init_builtins(SKState* pSK);

static int expr_level = 0;

#define PARSE_LOG(...) (void)(g_current_sk && g_current_sk->parse_log_enabled && indent() && fprintf(stderr, __VA_ARGS__))

//------------------------
// pretty print parse tree
//------------------------
static int indent()
{
    for (int i = 0; i < indent_level; i++)
    {
        fprintf(stderr, "  ");
    }
    
	fprintf(stderr, "|_ ");
    indent_level++;
    return 1;
}

//------------------------
// parse tree indent
//------------------------
static void unindent() 
{ 
    if (!g_current_sk || !g_current_sk->parse_log_enabled) return;

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
    else if (g_current_sk && g_current_sk->file_stack_ptr >= 0)
        fprintf(stderr, TERM_RED"ERROR: '%s' in file '%s' near line %d\n"TERM_RESET, s, 
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].filename,
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].yylineno);
    else
        fprintf(stderr, TERM_RED"ERROR: '%s'\n"TERM_RESET, s);
    
    if (g_current_sk)
        g_current_sk->err_count++;
}

//------------------------
// warning routine
//------------------------
void yywarning(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_YELLOW"WARNING: '%s'\n"TERM_RESET, s);
    else if (g_current_sk && g_current_sk->file_stack_ptr >= 0)
        fprintf(stderr, TERM_YELLOW"WARNING: '%s' in file '%s' near line %d\n"TERM_RESET, s, 
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].filename,
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].yylineno);
    else
        fprintf(stderr, TERM_YELLOW"WARNING: '%s'\n"TERM_RESET, s);
    
    if (g_current_sk)
        g_current_sk->warn_count++;
}

//------------------------
// info routine
//------------------------
void yyinfo(const char *s)
{
    if (yyin == stdin)
        fprintf(stderr, TERM_GREEN"INFO: '%s'\n"TERM_RESET, s);
    else if (g_current_sk && g_current_sk->file_stack_ptr >= 0)
        fprintf(stderr, TERM_GREEN"INFO: '%s' in file '%s' near line %d\n"TERM_RESET, s, 
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].filename,
                g_current_sk->file_stack[g_current_sk->file_stack_ptr].yylineno);
    else
        fprintf(stderr, TERM_GREEN"INFO: '%s'\n"TERM_RESET, s);
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
		lookahead = yylex(g_current_sk);
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

    return sk_strcasecmp(s1->name, s2->name);
}

//------------------------
// print out symbols
//------------------------
void dump_symbols(SKState* pSK)
{
    if (!pSK) return;

    // alpha sort symbols
    qsort(pSK->symbols, pSK->symbol_count, sizeof(Symbol_t), compare_fn);

    puts("\nSymbol table\n------------");
    for (int i = 0; i < pSK->symbol_count; i++)
    {
        printf("%10s\t%04X\n", pSK->symbols[i].name, pSK->symbols[i].value);
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

        vm_emit(&g_current_sk->vm, OP_TRUE);
        match(TOKEN_TRUE);
        unindent();
        break;

    case TOKEN_FALSE:
        PARSE_LOG("TOKEN_FALSE\n");

        vm_emit(&g_current_sk->vm, OP_FALSE);
        match(TOKEN_FALSE);
        unindent();
        break;

    case NUMBER:
        // handle number
        PARSE_LOG("NUMBER: %d\n", yylval.ival);

		vm_emit_number(&g_current_sk->vm, yylval.ival);
        match(NUMBER);
        unindent();
        break;

    case STRING:
        // TODO - use yylval.word and handle interned strings
        PARSE_LOG("STRING: '%s'\n", g_current_sk->symbols[yylval.symbol].name);

		vm_emit_string(&g_current_sk->vm, g_current_sk->symbols[yylval.symbol].name);
        match(STRING);
        unindent();
        break;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit_push_object(&g_current_sk->vm, NULL);
        match(NIL);
        unindent();
        break;

    case ID:
        {
            PARSE_LOG("ID: '%s'\n", yylval.word);

            // check if var is local, if so emit stack read
			Object* obj = vm_get_local_var(&g_current_sk->vm, yylval.word);

            // handle variable and check for undefined symbol
            Object* key = vm_get_string(&g_current_sk->vm, yylval.word);
            match(ID);

            if (!key)
            {
                yyerror("entity undefined");
            }
            else if (IS_NUM(obj))
            {
                vm_emit_number(&g_current_sk->vm, AS_NUM(obj));
                vm_emit(&g_current_sk->vm, OP_GET_LOCAL);
            }
            else
            {
                vm_emit_push_object(&g_current_sk->vm, key);
                vm_emit(&g_current_sk->vm, OP_LOAD);
            }

            unindent();
        }
        break;
    
    default:
        yyerror("syntax error");
        match(lookahead);
        unindent();
        return 0;
    }

    unindent();
    return 1;
}

//-----------------------------------------------
//  <formals> ==> (<variable>*) | <variable>
//-----------------------------------------------
static int formals(Procedure* proc)
{
	PARSE_LOG("formals(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    int arity = 0;
    while (lookahead == ID)
    {
        arity++;
		vm_set_var(&g_current_sk->vm, yylval.word, MAKE_NUM(arity));   // add parameter to function scope
        match(ID);
    }

    if (g_current_sk->vm.current_scope)
    {
        Environment* env = g_current_sk->vm.current_scope;

        // update all bindings in the current environment
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
        {
			ht_add(env->bindings, env_key, MAKE_NUM(arity - AS_NUM(env_value) + 1)); // set parameter stack offset
        }
    }

    if (proc)
        proc->arity = arity;
	
    unindent();
    return 0;
}

//-----------------------------------------------
//  <body> ==> <definition>* <sequence>
//  <sequence> ==> <command>* <expression>
//  <command> ==> <expression>
//-----------------------------------------------
static int local_body()
{
	PARSE_LOG("local_body(): lookahead = %d '%c'\n", lookahead, (char)lookahead);
    
	// TODO  - handle local definitions

	while (lookahead == '(')
        expr();

	unindent();
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
        
        vm_new_environment(&g_current_sk->vm);   // create new environment for function scope

        // function name
        Object* proc_name = vm_new_string(&g_current_sk->vm, yylval.word);
		vm_push(&g_current_sk->vm, proc_name);                    // stash function name on stack in case of GC
            Object* proc_obj = vm_new_proc(&g_current_sk->vm);
		vm_pop(&g_current_sk->vm);                                // remove function name from stack

        Procedure* proc = AS_OBJ(Procedure, proc_obj);
        proc->name = (String*)proc_name;               // set function name

        CodeChunk* prev_chunk = vm_set_current_chunk(&proc->chunk);

		if (!vm_define_global(&g_current_sk->vm, proc_name, proc_obj)) // TODO - add to current scope instead of global
            yyerror("failed to add global var");

        match(ID);
		
        // parameters
        formals(proc);

        match(')');
        
        // function body
        local_body();

        // return from function
        vm_emit_number(&g_current_sk->vm, proc->arity);
        vm_emit(&g_current_sk->vm, OP_RET);
        vm_set_current_chunk(prev_chunk);

        vm_free_environment(&g_current_sk->vm); // pop function environment off chain
	}
    else
    {
		// variable definition
        Object* key = vm_new_string(&g_current_sk->vm, yylval.word);
		vm_define_global(&g_current_sk->vm, key, NULL);   // needs to be in global table before expr() in case of GC
        vm_emit_push_object(&g_current_sk->vm, key);

        match(ID);
        expr();
        vm_emit(&g_current_sk->vm, OP_STORE);
    }

    unindent();
    return 0;
}

//-----------------------------------------------
// lambda := '(' 'lambda' ( lambda-definition ) ')'
// lambda-definition := '(' parameter* ')' s-expr
// test: (define a (lambda (x) (* 2 2)))
//-----------------------------------------------
static int lambda()
{
    PARSE_LOG("lambda(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(LAMBDA);

    Object* proc_obj = vm_new_proc(&g_current_sk->vm);

    // put the procedure object on the stack for GC safety
    vm_push(&g_current_sk->vm, proc_obj);

    Procedure* proc = AS_OBJ(Procedure, proc_obj);
	proc->name = (String*)vm_new_string(&g_current_sk->vm, "<lambda>");
    vm_define_global(&g_current_sk->vm, (Object*)proc->name, NULL);   // ensure name is in global table

    CodeChunk* prev_chunk = vm_set_current_chunk(&proc->chunk);

    if (lookahead == '(')
    {
        match('(');
    
        // parameters
        formals(proc);

        match(')');
    }
    else
    {
        if (lookahead == ID)
        {
            // single parameter
            proc->arity = 1;
            match(ID);
        }
        else
            proc->arity = 0;
    }
     
    // function body
    local_body();

    vm_emit_push_object(&g_current_sk->vm, proc_obj);

    // return from function
    vm_emit(&g_current_sk->vm, OP_RET);
    vm_set_current_chunk(prev_chunk);

    // vm_pop(&g_current_sk->vm);    // remove proc_obj from stack

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

    vm_emit_number(&g_current_sk->vm, nargs);
    vm_emit(&g_current_sk->vm, OP_LIST);

    unindent();
    return 0;
}

//-----------------------------------------------
// load_expr := '(' 'load' STRING ')'
//-----------------------------------------------
static int load_expr()
{
    PARSE_LOG("load_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    match(LOAD);

    if (lookahead != STRING)
    {
        yyerror("expected string filename in 'load' expression");
        unindent();
        return 0;
    }

    // get filename
    const char* filename = g_current_sk->symbols[yylval.symbol].name;
    match(STRING);

    // load (parse) the file
    push_filename_stack(g_current_sk, filename);

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
    Object* key = vm_get_string(&g_current_sk->vm, yylval.word);
    if (!key)
    {
        yyerror("entity undefined");
        unindent();
        return 0;
    }

    match(ID);

    if (key)
    {
        vm_emit_push_object(&g_current_sk->vm, key);
        expr();
        vm_emit(&g_current_sk->vm, OP_STORE);
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

    expr();                                 // condition
	vm_emit(&g_current_sk->vm, OP_BNT);                   // branch if not true
	int else_addr = vm_emit(&g_current_sk->vm, 0);        // reserve space for else clause address

    expr();                                 // then branch expression
	vm_emit(&g_current_sk->vm, OP_BRA);                   // jump to endif
    int endif_addr = vm_emit(&g_current_sk->vm, 0);       // reserve space for endif address
    vm_get_current_chunk()->code[else_addr] = vm_get_current_chunk()->count - else_addr;

#ifdef IF_ELSE_REQUIRED
    expr();                                 // else branch expression
#else
    if (lookahead != ')')
    {
        expr();                             // else branch expression
    }
#endif

    // backpatch BNT address
    vm_get_current_chunk()->code[endif_addr] = vm_get_current_chunk()->count - endif_addr;

    unindent();
    return 0;
}

//-----------------------------------------------
// cond_expr := '(' 'cond' ( expr expr )* ')'
//-----------------------------------------------
static int cond_expr()
{
    PARSE_LOG("cond_expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);
    int addr_list[MAX_REL_CLAUSES];
    int terms = 0;

    match(COND);

    while (lookahead != ')')
    {
        match('(');

        if (lookahead == ELSE)
        {
            match(ELSE);
            expr();                                // action/consequence
        }
        else
        {
            expr();                                // test/condition
            vm_emit(&g_current_sk->vm, OP_BNT);                  // branch if not true
            int next_addr = vm_emit(&g_current_sk->vm, 0);       // reserve space for next clause address

            expr();                                // action/consequence

            // return last expr value
            vm_emit(&g_current_sk->vm, OP_BRA);                  // jump to end
            addr_list[terms++] = vm_emit(&g_current_sk->vm, 0);  // reserve space for endif address
            if (terms >= MAX_REL_CLAUSES)
            {
                yyerror("too many terms in 'and' expression");
                break;
            }

            // backpatch BNT address
            vm_get_current_chunk()->code[next_addr] = vm_get_current_chunk()->count - next_addr;
        }

        match(')');
    }

    // backpatch GOTO address
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
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
    vm_emit_number(&g_current_sk->vm, count);

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

    vm_emit_push_object(&g_current_sk->vm, NULL);  // push a dummy nil value onto the stack

    while (lookahead != DONE && lookahead != ')')
    {
        vm_emit(&g_current_sk->vm, OP_POP);       // pop the duplicated value
        
        expr();

        vm_emit(&g_current_sk->vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&g_current_sk->vm, OP_BNT);       // if any term is false, branch to end

        addr_list[terms++] = vm_emit(&g_current_sk->vm, 0);      // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror("too many terms in 'and' expression");
            break;
		}
    }

	// return last expr value
	vm_emit(&g_current_sk->vm, OP_BRA);
    int end_addr = vm_emit(&g_current_sk->vm, 0);     // reserve space for end address

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
    }

    vm_emit(&g_current_sk->vm, OP_POP);  // pop the duplicated value
    vm_emit(&g_current_sk->vm, OP_FALSE);

    vm_get_current_chunk()->code[end_addr] = vm_get_current_chunk()->count - end_addr;
    vm_emit(&g_current_sk->vm, OP_NOP);

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

    vm_emit_push_object(&g_current_sk->vm, NULL);      // push a dummy nil value onto the stack

    while (lookahead != DONE && lookahead != ')')
    {
        vm_emit(&g_current_sk->vm, OP_POP);       // pop the duplicated value
        
        expr();
        
        vm_emit(&g_current_sk->vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&g_current_sk->vm, OP_BNF);       // if any term is not false, branch to end

        addr_list[terms++] = vm_emit(&g_current_sk->vm, 0);  // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror("too many terms in 'or' expression");
            break;
        }
    }

    vm_emit(&g_current_sk->vm, OP_POP);      // discard the duplicated value
    vm_emit(&g_current_sk->vm, OP_FALSE);

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
    }

    vm_emit(&g_current_sk->vm, OP_NOP);

    // return last expr value
    return 0;
}

//-----------------------------------------------
// program := <commmand | definition>*
//-----------------------------------------------
static int expr()
{
    PARSE_LOG("expr(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    expr_level++;

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

        case LOAD:
            load_expr();
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
                Object* key = vm_get_string(&g_current_sk->vm, yylval.word);
                
                match(lookahead);

                if (key)
                {
                    int local_fn = FALSE;

                    // look first for a local variable or param
                    Object* proc = vm_get_local_var(&g_current_sk->vm, ((String*)key)->value);

					// if not found, look in globals
                    if (proc && IS_NUM(proc))
                    {
                        local_fn = TRUE;
                    }
                    else
                        proc = vm_get_global(&g_current_sk->vm, key);

                    if (IS_FUNCTION(proc))
                    {
                        expr_tail(); // parse function arguments (if any)

                        vm_emit_push_object(&g_current_sk->vm, proc);
                        vm_emit(&g_current_sk->vm, OP_CALL);
//                        vm_emit(&g_current_sk->vm, (VMCode_t)proc);
                    }
                    else if (local_fn)
                    {
                        expr_tail(); // parse function arguments (if any)

                        vm_emit_number(&g_current_sk->vm, AS_NUM(proc));
                        vm_emit(&g_current_sk->vm, OP_GET_LOCAL);
                        vm_emit(&g_current_sk->vm, OP_CALL);
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

    expr_level--;

    // print result of top-level exprs
    if (expr_level == 0)
        vm_emit(&g_current_sk->vm, OP_PRINT_TOS);   // print and discard top-level expr result

    return 0;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(SKState *pSK)
{
    //PARSE_LOG("yyparse(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    if (yyin == NULL)
        return 0;

    // set current parser state
    g_current_sk = pSK;

    // prime the pump
    lookahead = yylex(pSK);

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
        Object* key = vm_new_string(&g_current_sk->vm, funcs[i].name);
        vm_push(&g_current_sk->vm, key);  // for GC safety

        Object* native_fn = vm_newObject(&g_current_sk->vm, sizeof(NativeFunction), OBJ_NATIVE_FN);
        NativeFunction* nf = AS_OBJ(NativeFunction, native_fn);
        if (!nf)
        {
            yyerror("failed to alloc object");
            vm_pop(&g_current_sk->vm);    // remove key
            return SK_FALSE;
        }

        nf->func = funcs[i].func;
        nf->arity = funcs[i].arity;
        nf->name = (String*) key;
        
        vm_define_global(&g_current_sk->vm, vm_pop(&g_current_sk->vm), native_fn);

        LOG("Registered built-in function '%s'\n", funcs[i].name);
    }

    return 0;
}

//-----------------------------------------------
// init skeme
//-----------------------------------------------
int sk_init(SKState *pSK, int argc, char* argv[])
{
    if (!pSK) return FALSE;

    // initialize SKState fields
    memset(pSK, 0, sizeof(SKState));
    
    // allocate dynamic arrays
    pSK->symbol_capacity = 256;
    pSK->symbols = (Symbol_t*)calloc(pSK->symbol_capacity, sizeof(Symbol_t));
    if (!pSK->symbols)
    {
        fprintf(stderr, "Failed to allocate symbol table!\n");
        return FALSE;
    }
    
    pSK->file_stack_capacity = 10;
    pSK->file_stack = (FileNode*)calloc(pSK->file_stack_capacity, sizeof(FileNode));
    if (!pSK->file_stack)
    {
        fprintf(stderr, "Failed to allocate file stack!\n");
        free(pSK->symbols);
        return FALSE;
    }

    pSK->symbol_count = 0;
    pSK->file_stack_ptr = 0;  // start at 0 with sentinel entry
    pSK->err_count = 0;
    pSK->warn_count = 0;
    pSK->parse_log_enabled = 0;

    // initialize sentinel entry at index 0
    pSK->file_stack[0].fptr = NULL;
    pSK->file_stack[0].filename = sk_strdup(SK_UNKNOWN_FILE);
    pSK->file_stack[0].yylineno = 0;
    pSK->file_stack[0].column = 0;

    // set current state for parser
    g_current_sk = pSK;

    // initialize the VM
    if (VM_OK != vm_init(&pSK->vm))
    {
        fprintf(stderr, "Failed to initialize VM!\n");
        free(pSK->symbols);
        free(pSK->file_stack);
        return FALSE;
    }

    // set back-pointer from VM to SKState
    pSK->vm.sk_state = pSK;

    // register built-in functions
    if (SK_TRUE != sk_init_builtins(pSK))
    {
        fprintf(stderr, "Failed to register built-in functions!\n");
        free(pSK->symbols);
        free(pSK->file_stack);
        return FALSE;
    }

    // setup environment vars
    Pair* alist = NULL;
    Pair* pair = NULL;

    for (int i = argc - 1; i >= 0; i--)
    {
        vm_push(&pSK->vm, (Object*)pair);    // stash pair on stack to prevent premature GC
        alist = (Pair*)vm_newObject(&pSK->vm, sizeof(Pair), OBJ_PAIR);
		vm_push(&pSK->vm, (Object*)alist); // stash alist on stack to prevent premature GC
        alist->car = vm_new_string(&pSK->vm, argv[i]);
        vm_pop(&pSK->vm);                    // discard stashed list
        vm_pop(&pSK->vm);                    // discard stashed pair

        alist->cdr = (Object*)pair;

        pair = alist;
    }
    
    vm_push(&pSK->vm, (Object*)alist);
    Object* key = vm_new_string(&pSK->vm, "__env__");
    vm_pop(&pSK->vm);
    vm_define_global(&pSK->vm, key, (Object*)alist);

    return TRUE;
}

//-----------------------------------------------
// shutdown skeme
//-----------------------------------------------
int sk_shutdown(SKState *pSK)
{
    if (!pSK) return FALSE;

    // shutdown VM
    vm_shutdown(&pSK->vm);

    // free symbol table
    if (pSK->symbols)
    {
        // free individual symbol names and filenames
        for (int i = 0; i < pSK->symbol_count; i++)
        {
            if (pSK->symbols[i].name)
                free((void*)pSK->symbols[i].name);
            if (pSK->symbols[i].filename && strcmp(pSK->symbols[i].filename, SK_NONAME_FILE) != 0)
                free((void*)pSK->symbols[i].filename);
        }
        free(pSK->symbols);
        pSK->symbols = NULL;
    }

    // free file stack
    if (pSK->file_stack)
    {
        // free sentinel entry filename if it exists
        if (pSK->file_stack[0].filename)
            free(pSK->file_stack[0].filename);
        
        free(pSK->file_stack);
        pSK->file_stack = NULL;
    }

    g_current_sk = NULL;
    return TRUE;
}

//-----------------------------------------------
// parse the input
//-----------------------------------------------
int sk_parse(SKState *pSK)
{
    int result = yyparse(pSK);
    vm_emit(&g_current_sk->vm, OP_HALT);
    return result;
}

