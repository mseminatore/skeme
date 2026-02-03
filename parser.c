#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

#define LOOKAHEAD   (pSK->lookahead)

// forward declarations
static int expr(SKState* pSK);

#define PARSE_LOG(...) (void)(pSK && pSK->parse_log_enabled && indent(pSK) && fprintf(stderr, __VA_ARGS__))

//------------------------
// pretty print parse tree
//------------------------
static int indent(SKState* pSK)
{
    for (int i = 0; i < pSK->indent_level; i++)
    {
        fprintf(stderr, "  ");
    }
    
	fprintf(stderr, "|_ ");
    pSK->indent_level++;
    return 1;
}

//------------------------
// parse tree indent
//------------------------
static void unindent(SKState* pSK) 
{ 
    if (!pSK || !pSK->parse_log_enabled) return;

    pSK->indent_level--;
    if (pSK->indent_level < 0)
        pSK->indent_level = 0;
}

//------------------------
// error routine
//------------------------
//------------------------
// Helper function to log with optional stdin context clearing
//------------------------
static void log_with_stdin_check(SKState* pSK, SKLogLevel level, const char *s, 
                                   const char* fallback_color, const char* level_str)
{
    if (pSK) {
        // Check if stdin to avoid file context
        if (pSK->yyin == stdin) {
            // Clear file context for stdin
            char* saved_file = pSK->file_stack_ptr >= 0 ? 
                               pSK->file_stack[pSK->file_stack_ptr].filename : NULL;
            int saved_line = pSK->file_stack_ptr >= 0 ?
                             pSK->file_stack[pSK->file_stack_ptr].yylineno : 0;
            
            // Temporarily clear for stdin
            if (pSK->file_stack_ptr >= 0) {
                pSK->file_stack[pSK->file_stack_ptr].filename = NULL;
                pSK->file_stack[pSK->file_stack_ptr].yylineno = 0;
            }
            
            sk_log(pSK, level, "%s", s);
            
            // Restore
            if (pSK->file_stack_ptr >= 0) {
                pSK->file_stack[pSK->file_stack_ptr].filename = saved_file;
                pSK->file_stack[pSK->file_stack_ptr].yylineno = saved_line;
            }
        } else {
            sk_log(pSK, level, "%s", s);
        }
    } else {
        fprintf(stderr, "%s%s: '%s'\n"TERM_RESET, fallback_color, level_str, s);
    }
}

//------------------------
// error routine
//------------------------
void yyerror(SKState* pSK, const char *s)
{
    log_with_stdin_check(pSK, SK_LOG_ERROR, s, TERM_RED, "ERROR");
}

//------------------------
// warning routine
//------------------------
void yywarning(const char *s)
{
    extern SKState* g_current_sk;
    log_with_stdin_check(g_current_sk, SK_LOG_WARNING, s, TERM_YELLOW, "WARNING");
}

//------------------------
// info routine
//------------------------
void yyinfo(const char *s)
{
    extern SKState* g_current_sk;
    log_with_stdin_check(g_current_sk, SK_LOG_INFO, s, TERM_GREEN, "INFO");
}

//---------------------------------------------------
// error message for unexpected tokens
//---------------------------------------------------
static void expected(SKState* pSK, int token)
{
	if (token < 256)
		yyerror(pSK, "unexpected character"); //'%c'", token);
	else
		yyerror(pSK, "expected token"); //'%s'", getLexemeFromToken(token));
}

//---------------------------------------------------
// if token is matched continue, otherwise error
//---------------------------------------------------
static int match(SKState* pSK, int token)
{
	if (LOOKAHEAD == token)
	{
		LOOKAHEAD = yylex(pSK);
	}
	else
	{
		expected(pSK, token);
	}

	return LOOKAHEAD;
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
static int atom(SKState* pSK)
{
    PARSE_LOG("atom(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    switch (LOOKAHEAD)
    {
    case '(':
        expr(pSK);
        break;

    case TOKEN_TRUE:
        PARSE_LOG("TOKEN_TRUE\n");

        vm_emit(&pSK->vm, OP_TRUE);
        match(pSK, TOKEN_TRUE);
        unindent(pSK);
        break;

    case TOKEN_FALSE:
        PARSE_LOG("TOKEN_FALSE\n");

        vm_emit(&pSK->vm, OP_FALSE);
        match(pSK, TOKEN_FALSE);
        unindent(pSK);
        break;

    case NUMBER:
        // handle number
        PARSE_LOG("NUMBER: %d\n", yylval.ival);

		vm_emit_number(&pSK->vm, yylval.ival);
        match(pSK, NUMBER);
        unindent(pSK);
        break;

    case STRING:
        // TODO - use yylval.word and handle interned strings
        PARSE_LOG("STRING: '%s'\n", pSK->symbols[yylval.symbol].name);

		vm_emit_string(&pSK->vm, pSK->symbols[yylval.symbol].name);
        match(pSK, STRING);
        unindent(pSK);
        break;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit_push_object(&pSK->vm, NULL);
        match(pSK, NIL);
        unindent(pSK);
        break;

    case ID:
        {
            PARSE_LOG("ID: '%s'\n", yylval.word);

            // check if var is local, if so emit stack read
			Object* obj = vm_get_local_var(&pSK->vm, yylval.word);

            // handle variable and check for undefined symbol
            Object* key = vm_get_string(&pSK->vm, yylval.word);
            match(pSK, ID);

            if (!key)
            {
                yyerror(pSK, "entity undefined");
            }
            else if (IS_NUM(obj))
            {
                vm_emit_number(&pSK->vm, AS_NUM(obj));
                vm_emit(&pSK->vm, OP_GET_LOCAL);
            }
            else
            {
                vm_emit_push_object(&pSK->vm, key);
                vm_emit(&pSK->vm, OP_LOAD);
            }

            unindent(pSK);
        }
        break;
    
    default:
        yyerror(pSK, "syntax error");
        match(pSK, LOOKAHEAD);
        unindent(pSK);
        return 0;
    }

    unindent(pSK);
    return 1;
}

//-----------------------------------------------
//  <formals> ==> (<variable>*) | <variable>
//-----------------------------------------------
static int formals(SKState* pSK, Procedure* proc)
{
	PARSE_LOG("formals(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    int arity = 0;
    while (LOOKAHEAD == ID)
    {
        arity++;
		vm_set_var(&pSK->vm, yylval.word, MAKE_NUM(arity));   // add parameter to function scope
        match(pSK, ID);
    }

    if (pSK->vm.current_scope)
    {
        Environment* env = pSK->vm.current_scope;

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
	
    unindent(pSK);
    return 0;
}

//-----------------------------------------------
//  <body> ==> <definition>* <sequence>
//  <sequence> ==> <command>* <expression>
//  <command> ==> <expression>
//-----------------------------------------------
static int local_body(SKState* pSK)
{
	PARSE_LOG("local_body(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);
    
	// TODO  - handle local definitions

	while (LOOKAHEAD == '(')
        expr(pSK);

	unindent(pSK);
    return 0;
}

//-----------------------------------------------
// defintion := '(' 'define' ( function-definition | variable-definition ) ')'
// function-definition := '(' ID parameter* ')' s-expr
// variable-definition := ID s-expr
//-----------------------------------------------
static int definition(SKState* pSK)
{
    PARSE_LOG("definition(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, DEFINE);

    // look for a function definition or variable definition
    if (LOOKAHEAD == '(')
    {
		// function definition
		match(pSK, '(');
        
        vm_new_environment(&pSK->vm);   // create new environment for function scope

        // function name
        Object* proc_name = vm_new_string(&pSK->vm, yylval.word);
		vm_push(&pSK->vm, proc_name);                    // stash function name on stack in case of GC
            Object* proc_obj = vm_new_proc(&pSK->vm);
		vm_pop(&pSK->vm);                                // remove function name from stack

        Procedure* proc = AS_OBJ(Procedure, proc_obj);
        proc->name = (String*)proc_name;               // set function name

        CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

		if (!vm_define_global(&pSK->vm, proc_name, proc_obj)) // TODO - add to current scope instead of global
            yyerror(pSK, "failed to add global var");

        match(pSK, ID);
		
        // parameters
        formals(pSK, proc);

        match(pSK, ')');
        
        // function body
        local_body(pSK);

        // return from function
        vm_emit_number(&pSK->vm, proc->arity);
        vm_emit(&pSK->vm, OP_RET);
        vm_set_current_chunk(&pSK->vm, prev_chunk);

        vm_free_environment(&pSK->vm); // pop function environment off chain
	}
    else
    {
		// variable definition
        Object* key = vm_new_string(&pSK->vm, yylval.word);
		vm_define_global(&pSK->vm, key, NULL);   // needs to be in global table before expr() in case of GC
        vm_emit_push_object(&pSK->vm, key);

        match(pSK, ID);
        expr(pSK);
        vm_emit(&pSK->vm, OP_STORE);
    }

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// lambda := '(' 'lambda' ( lambda-definition ) ')'
// lambda-definition := '(' parameter* ')' s-expr
// test: (define a (lambda (x) (* 2 2)))
//-----------------------------------------------
static int lambda(SKState* pSK)
{
    PARSE_LOG("lambda(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, LAMBDA);

    Object* proc_obj = vm_new_proc(&pSK->vm);

    // put the procedure object on the stack for GC safety
    vm_push(&pSK->vm, proc_obj);

    Procedure* proc = AS_OBJ(Procedure, proc_obj);
	proc->name = (String*)vm_new_string(&pSK->vm, "<lambda>");
    vm_define_global(&pSK->vm, (Object*)proc->name, NULL);   // ensure name is in global table

    CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

    if (LOOKAHEAD == '(')
    {
        match(pSK, '(');
    
        // parameters
        formals(pSK, proc);

        match(pSK, ')');
    }
    else
    {
        if (LOOKAHEAD == ID)
        {
            // single parameter
            proc->arity = 1;
            match(pSK, ID);
        }
        else
            proc->arity = 0;
    }
     
    // function body
    local_body(pSK);

    vm_emit_push_object(&pSK->vm, proc_obj);

    // return from function
    vm_emit(&pSK->vm, OP_RET);
    vm_set_current_chunk(&pSK->vm, prev_chunk);

    // vm_pop(&pSK->vm);    // remove proc_obj from stack

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// list := '(' expr* ')'
//-----------------------------------------------
static int parse_list(SKState* pSK)
{
    PARSE_LOG("parse_list(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    int nargs = 0;

    match(pSK, LIST);

    while (LOOKAHEAD != ')')
    {
        expr(pSK);
        nargs++;
    }

    vm_emit_number(&pSK->vm, nargs);
    vm_emit(&pSK->vm, OP_LIST);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// load_expr := '(' 'load' STRING ')'
//-----------------------------------------------
static int load_expr(SKState* pSK)
{
    PARSE_LOG("load_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, LOAD);

    if (LOOKAHEAD != STRING)
    {
        yyerror(pSK, "expected string filename in 'load' expression");
        unindent(pSK);
        return 0;
    }

    // get filename
    const char* filename = pSK->symbols[yylval.symbol].name;
    match(pSK, STRING);

    // load (parse) the file
    push_filename_stack(pSK, filename);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// set := '(' 'set!' ID expr ')'
//-----------------------------------------------
static int set(SKState* pSK)
{
    PARSE_LOG("set(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, SET);
    // handle variable and check for undefined symbol
    Object* key = vm_get_string(&pSK->vm, yylval.word);
    if (!key)
    {
        yyerror(pSK, "entity undefined");
        unindent(pSK);
        return 0;
    }

    match(pSK, ID);

    if (key)
    {
        vm_emit_push_object(&pSK->vm, key);
        expr(pSK);
        vm_emit(&pSK->vm, OP_STORE);
    }
    else
        yyerror(pSK, "entity undefined");
    
    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// if_expr := '(' 'if' expr expr expr ')'
//-----------------------------------------------
static int if_expr(SKState* pSK)
{
    PARSE_LOG("if_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, IF);

    expr(pSK);                                 // condition
	vm_emit(&pSK->vm, OP_BNT);                   // branch if not true
	int else_addr = vm_emit(&pSK->vm, 0);        // reserve space for else clause address

    expr(pSK);                                 // then branch expression
	vm_emit(&pSK->vm, OP_BRA);                   // jump to endif
    int endif_addr = vm_emit(&pSK->vm, 0);       // reserve space for endif address
    vm_get_current_chunk()->code[else_addr] = vm_get_current_chunk()->count - else_addr;

#ifdef IF_ELSE_REQUIRED
    expr(pSK);                                 // else branch expression
#else
    if (LOOKAHEAD != ')')
    {
        expr(pSK);                             // else branch expression
    }
#endif

    // backpatch BNT address
    vm_get_current_chunk()->code[endif_addr] = vm_get_current_chunk()->count - endif_addr;

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// cond_expr := '(' 'cond' ( expr expr )* ')'
//-----------------------------------------------
static int cond_expr(SKState* pSK)
{
    PARSE_LOG("cond_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);
    int addr_list[MAX_REL_CLAUSES];
    int terms = 0;

    match(pSK, COND);

    while (LOOKAHEAD != ')')
    {
        match(pSK, '(');

        if (LOOKAHEAD == ELSE)
        {
            match(pSK, ELSE);
            expr(pSK);                                // action/consequence
        }
        else
        {
            expr(pSK);                                // test/condition
            vm_emit(&pSK->vm, OP_BNT);                  // branch if not true
            int next_addr = vm_emit(&pSK->vm, 0);       // reserve space for next clause address

            expr(pSK);                                // action/consequence

            // return last expr value
            vm_emit(&pSK->vm, OP_BRA);                  // jump to end
            addr_list[terms++] = vm_emit(&pSK->vm, 0);  // reserve space for endif address
            if (terms >= MAX_REL_CLAUSES)
            {
                yyerror(pSK, "too many terms in 'and' expression");
                break;
            }

            // backpatch BNT address
            vm_get_current_chunk()->code[next_addr] = vm_get_current_chunk()->count - next_addr;
        }

        match(pSK, ')');
    }

    // backpatch GOTO address
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
    }

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// expr_tail := expr*
//-----------------------------------------------
static int expr_tail(SKState* pSK)
{
    int count = 0;

    while (LOOKAHEAD != DONE && LOOKAHEAD != ')')
    {
        expr(pSK);
        count++;
    }

	// push number of arguments onto stack
    vm_emit_number(&pSK->vm, count);

    return count;
}

//-----------------------------------------------
// and_expr := '(' 'and' expr* ')'
//-----------------------------------------------
static int and_expr(SKState* pSK)
{
    PARSE_LOG("and_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);
    int addr_list[MAX_REL_CLAUSES];
    int terms = 0;

    match(pSK, AND);

    vm_emit_push_object(&pSK->vm, NULL);  // push a dummy nil value onto the stack

    while (LOOKAHEAD != DONE && LOOKAHEAD != ')')
    {
        vm_emit(&pSK->vm, OP_POP);       // pop the duplicated value
        
        expr(pSK);

        vm_emit(&pSK->vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&pSK->vm, OP_BNT);       // if any term is false, branch to end

        addr_list[terms++] = vm_emit(&pSK->vm, 0);      // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror(pSK, "too many terms in 'and' expression");
            break;
		}
    }

	// return last expr value
	vm_emit(&pSK->vm, OP_BRA);
    int end_addr = vm_emit(&pSK->vm, 0);     // reserve space for end address

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
    }

    vm_emit(&pSK->vm, OP_POP);  // pop the duplicated value
    vm_emit(&pSK->vm, OP_FALSE);

    vm_get_current_chunk()->code[end_addr] = vm_get_current_chunk()->count - end_addr;
    vm_emit(&pSK->vm, OP_NOP);

    return 0;
}

//-----------------------------------------------
// or_expr := '(' 'or' expr* ')'
//-----------------------------------------------
static int or_expr(SKState* pSK)
{
    PARSE_LOG("or_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);
    int addr_list[MAX_REL_CLAUSES];

    int terms = 0;

    match(pSK, OR);

    vm_emit_push_object(&pSK->vm, NULL);      // push a dummy nil value onto the stack

    while (LOOKAHEAD != DONE && LOOKAHEAD != ')')
    {
        vm_emit(&pSK->vm, OP_POP);       // pop the duplicated value
        
        expr(pSK);
        
        vm_emit(&pSK->vm, OP_DUP);       // duplicate the value for testing
		vm_emit(&pSK->vm, OP_BNF);       // if any term is not false, branch to end

        addr_list[terms++] = vm_emit(&pSK->vm, 0);  // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror(pSK, "too many terms in 'or' expression");
            break;
        }
    }

    vm_emit(&pSK->vm, OP_POP);      // discard the duplicated value
    vm_emit(&pSK->vm, OP_FALSE);

    // backpatch branch addresses
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[addr_list[i]] = vm_get_current_chunk()->count - addr_list[i];
    }

    vm_emit(&pSK->vm, OP_NOP);

    // return last expr value
    return 0;
}

//-----------------------------------------------
// program := <commmand | definition>*
//-----------------------------------------------
static int expr(SKState* pSK)
{
    PARSE_LOG("expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    pSK->expr_level++;

    if (LOOKAHEAD == '(')
    {
        match(pSK, '(');

        switch (LOOKAHEAD)
        {
        case DEFINE:
            definition(pSK);
            break;

        case LAMBDA:
            lambda(pSK);
            break;
        
        case LIST:
            parse_list(pSK);
            break;

        case SET:
            set(pSK);
            break;

        case LOAD:
            load_expr(pSK);
            break;

        case IF:
            if_expr(pSK);
            break;

        case AND:
            and_expr(pSK);
            break;

        case OR:
            or_expr(pSK);
            break;

        case COND:
            cond_expr(pSK);
			break;

        case '+':        case '-':
        case '*':
        case '/':
        case '<':
        case '>':
        case '=':
        case ID:
            {
                // handle native and regular function calls
                // handle variable and check for undefined symbol
                Object* key = vm_get_string(&pSK->vm, yylval.word);
                
                match(pSK, LOOKAHEAD);

                if (key)
                {
                    int local_fn = FALSE;

                    // look first for a local variable or param
                    Object* proc = vm_get_local_var(&pSK->vm, ((String*)key)->value);

					// if not found, look in globals
                    if (proc && IS_NUM(proc))
                    {
                        local_fn = TRUE;
                    }
                    else
                        proc = vm_get_global(&pSK->vm, key);

                    if (IS_FUNCTION(proc))
                    {
                        expr_tail(pSK); // parse function arguments (if any)

                        vm_emit_push_object(&pSK->vm, proc);
                        vm_emit(&pSK->vm, OP_CALL);
//                        vm_emit(&pSK->vm, (VMCode_t)proc);
                    }
                    else if (local_fn)
                    {
                        expr_tail(pSK); // parse function arguments (if any)

                        vm_emit_number(&pSK->vm, AS_NUM(proc));
                        vm_emit(&pSK->vm, OP_GET_LOCAL);
                        vm_emit(&pSK->vm, OP_CALL);
                    }
                    else
                    {
                        yyerror(pSK, "procedure expected!");
                    }
                }
                else
                    yyerror(pSK, "entity undefined");
            }
            break;
        }

        match(pSK, ')');
    }
    else
        atom(pSK);

    unindent(pSK);

    pSK->expr_level--;

    // print result of top-level exprs
    if (pSK->expr_level == 0)
        vm_emit(&pSK->vm, OP_PRINT_TOS);   // print and discard top-level expr result

    return 0;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(SKState *pSK)
{
    //PARSE_LOG("yyparse(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    if (pSK->yyin == NULL)
        return 0;

    // prime the pump
    LOOKAHEAD = yylex(pSK);

    pSK->indent_level = 0;

    // the top-level is zero or more s-expressions
	while (!pSK->bDone && LOOKAHEAD != DONE)
    {
        expr(pSK);
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
        Object* key = vm_new_string(&pSK->vm, funcs[i].name);
        vm_push(&pSK->vm, key);  // for GC safety

        Object* native_fn = vm_newObject(&pSK->vm, sizeof(NativeFunction), OBJ_NATIVE_FN);
        NativeFunction* nf = AS_OBJ(NativeFunction, native_fn);
        if (!nf)
        {
            yyerror(pSK, "failed to alloc object");
            vm_pop(&pSK->vm);    // remove key
            return SK_FALSE;
        }

        nf->func = funcs[i].func;
        nf->arity = funcs[i].arity;
        nf->name = (String*) key;
        
        vm_define_global(&pSK->vm, vm_pop(&pSK->vm), native_fn);

        LOG("Registered built-in function '%s'\n", funcs[i].name);
    }

    return 0;
}

