//---------------------------------------------------------
// parser.c - implementation of the parser for SK
//
// Copyright (c) 2025 Mark Seminatore
//----------------------------------------------------------

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "modules.h"
#include "term_colors.h"

#define LOOKAHEAD   (pSK->lookahead)

// forward declarations
static int expr(SKState* pSK);
static void quote_datum(SKState* pSK);

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
void yywarning(SKState* pSK, const char *s)
{
    log_with_stdin_check(pSK, SK_LOG_WARNING, s, TERM_YELLOW, "WARNING");
}

//------------------------
// info routine
//------------------------
void yyinfo(SKState* pSK, const char *s)
{
    log_with_stdin_check(pSK, SK_LOG_INFO, s, TERM_GREEN, "INFO");
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


//-----------------------------------------------
// The key for a reference to the global variable 'name'. Globals are looked
// up by name at run time, so a name that is not defined yet (a forward
// reference) is fine: it gets an unbound placeholder, which reading reports
// as an error if the definition has still not run. The table entry also
// keeps the interned name alive for the GC until then.
//-----------------------------------------------
static Object* global_key(SKState* pSK, const char* name)
{
    Object* key = vm_new_string(&pSK->vm, name);

    if (key && !vm_has_global(&pSK->vm, key))
        vm_define_global(&pSK->vm, key, MAKE_UNBOUND());

    return key;
}

//-----------------------------------------------
// paren nesting that a single token contributes
//-----------------------------------------------
static int token_depth(int token)
{
    return token == '(' ? 1 : (token == ')' ? -1 : 0);
}

//-----------------------------------------------
// Emit a read of the global 'key': OP_LOAD_GLOBAL key cached_value cache_version,
// with an empty cache (version 0 never matches; see op_load_global)
//-----------------------------------------------
static void emit_load_global(SKState* pSK, Object* key)
{
    vm_emit(&pSK->vm, OP_LOAD_GLOBAL);
    vm_emit(&pSK->vm, (VMCode_t)key);
    vm_emit(&pSK->vm, (VMCode_t)NULL);
    vm_emit(&pSK->vm, (VMCode_t)0);
}

//-----------------------------------------------
// Register an upvalue for the current lambda being parsed.
// Returns the upvalue index (deduplicating by name).
//-----------------------------------------------
static int register_upvalue(SKState* pSK, const char* name, int scope_depth, int local_index)
{
    // Check if already registered
    for (int i = 0; i < pSK->upvalue_count; i++) {
        if (strcmp(pSK->upvalues[i].name, name) == 0)
            return i;
    }

    if (pSK->upvalue_count >= MAX_UPVALUES) {
        yyerror(pSK, "too many upvalues in closure");
        return -1;
    }

    int idx = pSK->upvalue_count++;
    strncpy(pSK->upvalues[idx].name, name, sizeof(pSK->upvalues[idx].name) - 1);
    pSK->upvalues[idx].name[sizeof(pSK->upvalues[idx].name) - 1] = '\0';
    pSK->upvalues[idx].scope_depth = scope_depth;
    pSK->upvalues[idx].local_index = local_index;
    pSK->upvalues[idx].is_upvalue = 0;  // default: capture from parent's locals

    return idx;
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
        PARSE_LOG("NUMBER: %d\n", pSK->yylval.ival);

        if (pSK->yylval.ival == 0)
            vm_emit(&pSK->vm, OP_NUM_0);
        else if (pSK->yylval.ival == 1)
            vm_emit(&pSK->vm, OP_NUM_1);
        else if (pSK->yylval.ival == 2)
            vm_emit(&pSK->vm, OP_NUM_2);
        else
            vm_emit_number(&pSK->vm, pSK->yylval.ival);
        match(pSK, NUMBER);
        unindent(pSK);
        break;

    case FLOAT:
        // handle float literal
        PARSE_LOG("FLOAT: %g\n", pSK->yylval.fval);

        {
            Object* flt = vm_new_float(&pSK->vm, pSK->yylval.fval);
            vm_emit_push_object(&pSK->vm, flt);
        }
        match(pSK, FLOAT);
        unindent(pSK);
        break;

    case CHAR:
        // handle character literal
        PARSE_LOG("CHAR: '%c'\n", pSK->yylval.ival);

        vm_emit_push_object(&pSK->vm, MAKE_CHAR(pSK->yylval.ival));
        match(pSK, CHAR);
        unindent(pSK);
        break;

    case STRING:
        PARSE_LOG("STRING: '%s'\n", pSK->yylval.word);

		vm_emit_string(&pSK->vm, pSK->yylval.word);
        match(pSK, STRING);
        unindent(pSK);
        break;

    case NIL:
        PARSE_LOG("NIL\n");

        vm_emit_push_object(&pSK->vm, NULL);
        match(pSK, NIL);
        unindent(pSK);
        break;

    case QUOTE:
        // 'datum shorthand — QUOTE token comes from lexer when ' is seen
        match(pSK, QUOTE);
        quote_datum(pSK);
        unindent(pSK);
        break;

    // operators used as values, e.g. (map + a b) or (apply * xs), name the
    // native procedures of the same name; so does list, e.g. (apply list xs)
    case '+': case '-': case '*': case '/':
    case '<': case '>': case '=':
    case LIST:
    case ID:
        {
            PARSE_LOG("ID: '%s'\n", pSK->yylval.word);

            // check if var is local (with scope depth), if so emit stack read or upvalue
            int scope_depth = -1;
			Object* obj = vm_get_local_var_ex(&pSK->vm, pSK->yylval.word, &scope_depth);

            char var_name[BUF_SIZE];
            strncpy(var_name, pSK->yylval.word, sizeof(var_name) - 1);
            var_name[sizeof(var_name) - 1] = '\0';
            match(pSK, LOOKAHEAD);

            if (scope_depth == 0 && IS_NUM(obj))
            {
                // Local variable in current scope
                vm_emit(&pSK->vm, OP_GET_LOCAL);
                vm_emit(&pSK->vm, (VMCode_t)AS_NUM(obj));
            }
            else if (scope_depth > 0 && IS_NUM(obj))
            {
                // Variable from enclosing scope — capture as upvalue
                int uv_idx = register_upvalue(pSK, var_name, scope_depth, AS_NUM(obj));
                if (uv_idx >= 0) {
                    vm_emit(&pSK->vm, OP_GET_UPVALUE);
                    vm_emit(&pSK->vm, (VMCode_t)uv_idx);
                }
            }
            else
            {
                // global variable, looked up by name at run time
                emit_load_global(pSK, global_key(pSK, var_name));
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
//-----------------------------------------------
// Parameters: a b c, optionally followed by . rest, which receives the
// remaining arguments as a list. With rest_only the parameter list is a
// single identifier, as in (lambda args ...), which receives them all.
//-----------------------------------------------
static int formals(SKState* pSK, Procedure* proc, int rest_only)
{
	PARSE_LOG("formals(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    int arity = 0;
    int has_rest = 0;
    while (!rest_only && LOOKAHEAD == ID)
    {
        arity++;
		vm_set_var(&pSK->vm, pSK->yylval.word, MAKE_NUM(arity));   // add parameter to function scope
        match(pSK, ID);
    }

    if (rest_only || LOOKAHEAD == '.')
    {
        if (!rest_only)
            match(pSK, '.');
        arity++;
        has_rest = 1;
        vm_set_var(&pSK->vm, pSK->yylval.word, MAKE_NUM(arity));   // the rest parameter
        match(pSK, ID);
    }

    if (pSK->vm.current_scope)
    {
        Environment* env = pSK->vm.current_scope;

        // update all bindings in the current environment
        // Stack layout: [args..., ret_addr, saved_fp, fn] with fp pointing past fn
        // So args are at fp-4, fp-5, etc. (callee fn at fp-1, saved_fp at fp-2, ret_addr at fp-3)
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
        {
			ht_add(env->bindings, env_key, MAKE_NUM(arity - AS_NUM(env_value) + 3)); // +3 for ret_addr, saved_fp, fn
        }
    }

    if (proc)
    {
        proc->arity = arity;
        proc->has_rest = has_rest;
    }

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
//  <body> ==> <definition>* <sequence>
//  <sequence> ==> <command>* <expression>
//  <command> ==> <expression>
//-----------------------------------------------
//-----------------------------------------------
// Is the CALL/ICALL at call_offset in tail position?
//
// It is only if, once the call returns, control reaches the end of the
// function body (where OP_RET is emitted) without executing anything else.
// Unconditional branches (OP_BRA, which if/cond use to jump past the other
// arms) are followed. Anything else after the call - a constant, a variable
// load, another call - means the call's value is discarded or consumed, so
// turning it into a TAILCALL would drop the caller's frame too early.
//-----------------------------------------------
static int call_is_in_tail_position(CodeChunk* chunk, int call_offset)
{
    VMCode_t op = chunk->code[call_offset];
    long p = call_offset + (op == OP_CALL ? 3 : 2);     // CALL: op,fn,nargs  ICALL/XCALL: op,nargs
    long end = (long)chunk->count;
    int hops = 0;

    while (p < end && hops++ < 10000)
    {
        if (chunk->code[p] == OP_NOP)                   // harmless label marker (and/or end)
        {
            p++;
            continue;
        }
        if (chunk->code[p] != OP_BRA || p + 1 >= end)
            return 0;
        p = (p + 1) + (long)chunk->code[p + 1];         // branch target = operand address + operand
    }

    return p == end;
}

typedef void (*BodyPrologue)(SKState* pSK);

static int body_defines(SKState* pSK);
static void lambda_ex(SKState* pSK, const char* name);

//-----------------------------------------------
// A function body. 'prologue', if given, compiles code at the start of
// the body (it may also consume part of it, as let* does), and takes part
// in tail-call patching like the body expressions.
//-----------------------------------------------
static int local_body_ex(SKState* pSK, BodyPrologue prologue)
{
	PARSE_LOG("local_body(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    // Parse the body expressions - last expression's value becomes return value
    //
    // TCO Strategy: We record the offset of each CALL/ICALL as we emit it.
    // After parsing all expressions, we patch the LAST call to TAILCALL.
    // This correctly handles both single and multi-expression bodies.

    // Save state for nested functions
    int saved_last_call_offset = pSK->last_call_offset;
    CodeChunk* saved_last_call_chunk = pSK->last_call_chunk;

    // Reset for this function body
    pSK->last_call_offset = -1;
    pSK->last_call_chunk = NULL;

    if (prologue)
        prologue(pSK);

    // leading internal defines compile the rest of the body themselves
    body_defines(pSK);

    // Parse all body expressions
    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        expr(pSK);
    }
    
    // TCO: Patch the last CALL to TAILCALL if there was one, and only if it is
    // really in tail position (see call_is_in_tail_position)
    if (pSK->last_call_offset >= 0 && pSK->last_call_chunk != NULL &&
        call_is_in_tail_position(pSK->last_call_chunk, pSK->last_call_offset))
    {
        VMCode_t opcode = pSK->last_call_chunk->code[pSK->last_call_offset];
        if (opcode == OP_CALL)
        {
            PARSE_LOG("TCO: patching CALL at offset %d to TAILCALL\n", pSK->last_call_offset);
            pSK->last_call_chunk->code[pSK->last_call_offset] = OP_TAILCALL;
        }
        else if (opcode == OP_ICALL)
        {
            PARSE_LOG("TCO: patching ICALL at offset %d to TAILCALL_I\n", pSK->last_call_offset);
            pSK->last_call_chunk->code[pSK->last_call_offset] = OP_TAILCALL_I;
        }
        else if (opcode == OP_XCALL)
        {
            PARSE_LOG("TCO: patching XCALL at offset %d to TAILCALL_X\n", pSK->last_call_offset);
            pSK->last_call_chunk->code[pSK->last_call_offset] = OP_TAILCALL_X;
        }
    }
    
    // Restore state for outer function
    pSK->last_call_offset = saved_last_call_offset;
    pSK->last_call_chunk = saved_last_call_chunk;

	unindent(pSK);
    return 0;
}

static int local_body(SKState* pSK)
{
    return local_body_ex(pSK, NULL);
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

    // Internal defines at the start of a body are compiled by body_defines.
    // Any other define inside a procedure is an error; parse it anyway so
    // the rest of the form can be read (its code is discarded).
    if (pSK->vm.current_scope != NULL)
    {
        yyerror(pSK, "define is only allowed at top level or at the start of a body");

        if (LOOKAHEAD == '(')
        {
            match(pSK, '(');
            char name[64];
            strncpy(name, pSK->yylval.word, 63);
            name[63] = '\0';
            match(pSK, ID);
            lambda_ex(pSK, name);
        }
        else
        {
            match(pSK, ID);
            expr(pSK);
        }

        unindent(pSK);
        return 0;
    }

    // look for a function definition or variable definition
    if (LOOKAHEAD == '(')
    {
		// function definition
		match(pSK, '(');

        vm_new_environment(&pSK->vm);   // create new environment for function scope

        // function name
        Object* proc_name = vm_new_string(&pSK->vm, pSK->yylval.word);
		vm_push(&pSK->vm, proc_name);                    // stash function name on stack in case of GC
            Object* proc_obj = vm_new_proc(&pSK->vm);
		vm_pop(&pSK->vm);                                // remove function name from stack

        Procedure* proc = AS_OBJ(Procedure, proc_obj);
        proc->name = (String*)proc_name;               // set function name

        CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

        // Store under a unique internal name as a GC root. The emitted code
        // refers to proc_obj directly, and the real name may be rebound by a
        // later define before this one runs.
        static int inner_def_id = 0;
        char gc_name[48];
        snprintf(gc_name, sizeof(gc_name), "<define-%d>", inner_def_id++);
        vm_push(&pSK->vm, proc_obj);        // keep proc reachable while the name is allocated
        Object* gc_key = vm_new_string(&pSK->vm, gc_name);
        vm_define_global(&pSK->vm, gc_key, proc_obj);
        vm_pop(&pSK->vm);

        // Also bind the real name now, so the body and later code see a
        // procedure while compiling. The definition takes effect in program
        // order through the OP_STORE emitted below. If the form has a syntax
        // error, yyparse drops it and undoes this binding (finish_define); the
        // previous value waits in <define-prev>, where the GC can see it.
        Object* prev = vm_get_global(&pSK->vm, proc_name);
        vm_push(&pSK->vm, prev ? prev : MAKE_UNBOUND());
        Object* prev_key = vm_new_string(&pSK->vm, "<define-prev>");
        prev = vm_pop(&pSK->vm);
        vm_define_global(&pSK->vm, prev_key, prev);
        pSK->define_name = proc_name;

        vm_define_global(&pSK->vm, proc_name, proc_obj);

        match(pSK, ID);
		
        // parameters
        formals(pSK, proc, 0);

        match(pSK, ')');
        
        // function body
        local_body(pSK);

        // return from function (arity is now stored in frame by CALL)
        vm_emit(&pSK->vm, OP_RET);
        vm_set_current_chunk(&pSK->vm, prev_chunk);

        vm_free_environment(&pSK->vm); // pop function environment off chain

        // bind the name when the define runs, so definitions and
        // redefinitions take effect in program order
        vm_emit_push_object(&pSK->vm, proc_name);
        vm_emit_push_object(&pSK->vm, proc_obj);
        vm_emit(&pSK->vm, OP_STORE);
	}
    else
    {
		// variable definition: the value is stored when the define runs. An
        // existing value is left alone until then; it may be used by expr(),
        // as in (define x (+ x 1)). global_key also roots the name for the GC.
        Object* key = global_key(pSK, pSK->yylval.word);
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
    lambda_ex(pSK, NULL);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// Compile a procedure and emit code that pushes it (or its closure). With
// a name, as for an internal (define (name param ...) body ...), the caller
// has matched '(' and the name, and the parameters follow; without one,
// LOOKAHEAD is at the lambda's parameter list.
//-----------------------------------------------
static void lambda_ex(SKState* pSK, const char* name)
{
    // Save outer lambda's upvalue state (for nested lambdas)
    ParseUpvalue saved_upvalues[MAX_UPVALUES];
    int saved_upvalue_count = pSK->upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(saved_upvalues, pSK->upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);
    pSK->upvalue_count = 0;

    vm_new_environment(&pSK->vm);   // create new environment for lambda scope

    Object* proc_obj = vm_new_proc(&pSK->vm);

    // put the procedure object on the stack for GC safety
    vm_push(&pSK->vm, proc_obj);

    Procedure* proc = AS_OBJ(Procedure, proc_obj);
    
    // Give each lambda a unique internal name for GC root storage
    static int lambda_id = 0;
    char lambda_name[32];
    snprintf(lambda_name, sizeof(lambda_name), "<lambda-%d>", lambda_id++);
	proc->name = (String*)vm_new_string(&pSK->vm, lambda_name);
    vm_define_global(&pSK->vm, (Object*)proc->name, proc_obj);   // store in globals as GC root
    if (name)
        proc->name = (String*)vm_new_string(&pSK->vm, name);     // for messages; reachable from proc

    CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

    if (name)
    {
        // (define (name param ...) ...): the parameters run to the ')'
        formals(pSK, proc, 0);
        match(pSK, ')');
    }
    else if (LOOKAHEAD == '(')
    {
        match(pSK, '(');

        // parameters
        formals(pSK, proc, 0);

        match(pSK, ')');
    }
    else if (LOOKAHEAD == ID)
    {
        // (lambda args ...): args is the list of all the arguments
        formals(pSK, proc, 1);
    }
    else
    {
        // (lambda () ...): "()" lexes as one token. No parameters to parse;
        // the next token already belongs to the body.
        if (LOOKAHEAD == NIL)
            match(pSK, NIL);
        else
            yyerror(pSK, "lambda: expected a parameter list");
        proc->arity = 0;
        proc->has_rest = 0;
    }
     
    // function body
    local_body(pSK);

    // return from function (arity is now stored in frame by CALL)
    vm_emit(&pSK->vm, OP_RET);
    vm_set_current_chunk(&pSK->vm, prev_chunk);

    vm_free_environment(&pSK->vm); // pop lambda environment off chain

    // Capture upvalue info before restoring outer state
    int num_upvalues = pSK->upvalue_count;
    ParseUpvalue captured[MAX_UPVALUES];
    if (num_upvalues > 0)
        memcpy(captured, pSK->upvalues, sizeof(ParseUpvalue) * num_upvalues);

    // Restore outer lambda's upvalue state
    pSK->upvalue_count = saved_upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(pSK->upvalues, saved_upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);

    // Propagate deep captures: if an inner lambda captured a variable at depth > 1,
    // the parent (us) must also capture it as an upvalue so we can pass it along.
    for (int i = 0; i < num_upvalues; i++)
    {
        if (captured[i].scope_depth > 1)
        {
            // Register this variable as an upvalue in the OUTER (parent) lambda
            int parent_uv_idx = register_upvalue(pSK, captured[i].name,
                captured[i].scope_depth - 1, captured[i].local_index);
            // Update captured entry: at closure creation time, get from parent's upvalue
            captured[i].local_index = parent_uv_idx;
            captured[i].is_upvalue = 1;
        }
    }

    if (num_upvalues > 0)
    {
        // Emit closure creation in the OUTER context.
        // OP_MAKE_CLOSURE expects stack (bottom→top): [upval0, ..., upvalN-1, proc, count]

        // Push the captured variables (shared upvalues) from the enclosing scope
        for (int i = 0; i < num_upvalues; i++)
        {
            if (captured[i].is_upvalue)
            {
                // Variable was captured from grandparent+ — read from parent's upvalues
                vm_emit(&pSK->vm, OP_CAPTURE_UPVALUE);
                vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
            }
            else
            {
                // Variable is in immediate parent's locals
                vm_emit(&pSK->vm, OP_CAPTURE_LOCAL);
                vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
            }
        }

        // Then push the procedure object
        vm_emit_push_object(&pSK->vm, proc_obj);

        // Then emit the closure with upvalue count as immediate
        vm_emit(&pSK->vm, OP_MAKE_CLOSURE);
        vm_emit(&pSK->vm, (VMCode_t)num_upvalues);
    }
    else
    {
        // No upvalues — emit plain procedure push (original behavior)
        vm_emit_push_object(&pSK->vm, proc_obj);
    }

    // NOTE: We intentionally DO NOT pop proc_obj from the stack here!
    // It must stay on the stack as a GC root until parsing completes.
    // The bytecode contains a pointer to proc_obj, but GC doesn't scan bytecode.
    // Leaving it on stack keeps it alive during subsequent allocs that might trigger GC.
    // The stack will be cleared before execution starts.
}

//-----------------------------------------------
// list := '(' expr* ')'
//-----------------------------------------------
static int parse_list(SKState* pSK)
{
    PARSE_LOG("parse_list(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    int nargs = 0;

    match(pSK, LIST);

    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        expr(pSK);
        nargs++;
    }

    vm_emit(&pSK->vm, OP_LIST);
    vm_emit(&pSK->vm, (VMCode_t)nargs);

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

    // get filename - must copy before match() overwrites yylval
    char filename[BUF_SIZE];
    strncpy(filename, pSK->yylval.word, BUF_SIZE - 1);
    filename[BUF_SIZE - 1] = '\0';
    match(pSK, STRING);

    // load (parse) the file
    push_filename_stack(pSK, filename);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// quote_datum - recursively emit code to construct a quoted value
// Numbers, strings, bools, chars are self-evaluating.
// Identifiers become symbols. Lists become OP_LIST constructions.
//-----------------------------------------------
static void quote_datum(SKState* pSK)
{
    PARSE_LOG("quote_datum(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    if (LOOKAHEAD == '(')
    {
        match(pSK, '(');

        // empty list?
        if (LOOKAHEAD == ')')
        {
            match(pSK, ')');
            vm_emit_push_object(&pSK->vm, NULL);  // nil = empty list
            return;
        }

        // non-empty list: recursively quote each element
        int count = 0;
        while (LOOKAHEAD != ')' && LOOKAHEAD != '.' && LOOKAHEAD != DONE)
        {
            quote_datum(pSK);
            count++;
        }

        if (LOOKAHEAD == '.')
        {
            // dotted list (a b . c): one more datum is the final cdr
            match(pSK, '.');
            if (count == 0)
                yyerror(pSK, "nothing before '.' in quoted list");
            quote_datum(pSK);
            match(pSK, ')');
            vm_emit(&pSK->vm, OP_LIST_TAIL);
            vm_emit(&pSK->vm, (VMCode_t)count);
            return;
        }

        match(pSK, ')');
        vm_emit(&pSK->vm, OP_LIST);
        vm_emit(&pSK->vm, (VMCode_t)count);
    }
    else if (LOOKAHEAD == NUMBER)
    {
        vm_emit_number(&pSK->vm, pSK->yylval.ival);
        match(pSK, NUMBER);
    }
    else if (LOOKAHEAD == STRING)
    {
        vm_emit_string(&pSK->vm, pSK->yylval.word);
        match(pSK, STRING);
    }
    else if (LOOKAHEAD == TOKEN_TRUE)
    {
        vm_emit(&pSK->vm, OP_TRUE);
        match(pSK, TOKEN_TRUE);
    }
    else if (LOOKAHEAD == TOKEN_FALSE)
    {
        vm_emit(&pSK->vm, OP_FALSE);
        match(pSK, TOKEN_FALSE);
    }
    else if (LOOKAHEAD == CHAR)
    {
        vm_emit(&pSK->vm, OP_PUSH);
        vm_emit(&pSK->vm, (VMCode_t)MAKE_CHAR(pSK->yylval.ival));
        match(pSK, CHAR);
    }
    else if (LOOKAHEAD == NIL)
    {
        vm_emit_push_object(&pSK->vm, NULL);
        match(pSK, NIL);
    }
    else if (LOOKAHEAD == QUOTE)
    {
        if (strcmp(pSK->yylval.word, "'") == 0)
        {
            // nested apostrophe form, '('a) inside a quoted list: build (quote datum).
            // OP_LIST takes its elements in push order, so push the symbol first.
            match(pSK, QUOTE);
            vm_emit_symbol(&pSK->vm, "quote");
            quote_datum(pSK);
            vm_emit(&pSK->vm, OP_LIST);
            vm_emit(&pSK->vm, 2);
        }
        else
        {
            // the word quote inside quoted data is just a symbol: '(quote x)
            vm_emit_symbol(&pSK->vm, "quote");
            match(pSK, QUOTE);
        }
    }
    else if (LOOKAHEAD == ID || LOOKAHEAD == '+' || LOOKAHEAD == '-' ||
             LOOKAHEAD == '*' || LOOKAHEAD == '/' || LOOKAHEAD == '<' ||
             LOOKAHEAD == '>' || LOOKAHEAD == '=')
    {
        // identifiers and operators become symbols
        vm_emit_symbol(&pSK->vm, pSK->yylval.word);
        match(pSK, LOOKAHEAD);
    }
    else if (LOOKAHEAD == FLOAT)
    {
        Object* flt = vm_new_float(&pSK->vm, pSK->yylval.fval);
        vm_emit_push_object(&pSK->vm, flt);
        match(pSK, FLOAT);
    }
    else if (LOOKAHEAD == DEFINE || LOOKAHEAD == LAMBDA || LOOKAHEAD == LIST ||
             LOOKAHEAD == SET || LOOKAHEAD == IF || LOOKAHEAD == AND ||
             LOOKAHEAD == OR || LOOKAHEAD == COND || LOOKAHEAD == ELSE ||
             LOOKAHEAD == LOAD || LOOKAHEAD == BEGIN || LOOKAHEAD == CASE_TOKEN ||
             LOOKAHEAD == IMPORT || LOOKAHEAD == LET || LOOKAHEAD == LET_STAR ||
             LOOKAHEAD == LETREC || LOOKAHEAD == DO)
    {
        // language keywords are ordinary symbols inside quoted data,
        // e.g. '(if a b) or '(define (f x) x)
        vm_emit_symbol(&pSK->vm, pSK->yylval.word);
        match(pSK, LOOKAHEAD);
    }
    else
    {
        yyerror(pSK, "unexpected token in quoted expression");
        match(pSK, LOOKAHEAD);  // consume to avoid infinite loop
    }
}

//-----------------------------------------------
// quote_expr := '(' 'quote' datum ')'
// Also handles 'datum (shorthand) when called directly
//-----------------------------------------------
static int quote_expr(SKState* pSK)
{
    PARSE_LOG("quote_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, QUOTE);
    quote_datum(pSK);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// import_expr := '(' 'import' ID ')'
//-----------------------------------------------
static int import_expr(SKState* pSK)
{
    PARSE_LOG("import_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, IMPORT);

    if (LOOKAHEAD != ID)
    {
        yyerror(pSK, "expected module name in 'import' expression");
        unindent(pSK);
        return 0;
    }

    const char* module_name = pSK->yylval.word;

    if (sk_import_module(pSK, module_name) != 0)
        yyerror(pSK, "failed to import module");

    match(pSK, ID);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// do_expr := '(' 'do' ((var init step) ...) (test result ...) body ... ')'
// Implemented as: emit inits, create lambda frame, loop with BRA/BNT
//-----------------------------------------------
#define MAX_DO_BINDINGS 16
static int do_expr(SKState* pSK)
{
    PARSE_LOG("do_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, DO);

    // Parse bindings: ((var init step) ...)
    char bind_names[MAX_DO_BINDINGS][64];
    char step_strs[MAX_DO_BINDINGS][BUF_SIZE];
    int nbindings = 0;

    match(pSK, '(');
    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        match(pSK, '(');

        // var name
        strncpy(bind_names[nbindings], pSK->yylval.word, 63);
        bind_names[nbindings][63] = '\0';
        match(pSK, ID);

        // init expression — emit into outer chunk (init values)
        expr(pSK);

        // step expression — save as text for later re-parse
        step_strs[nbindings][0] = '\0';
        if (LOOKAHEAD != ')')
        {
            int depth = 0;
            int pos = 0;
            if (LOOKAHEAD == '(')
            {
                depth = 1;
                step_strs[nbindings][pos++] = '(';
                match(pSK, '(');

                while (depth > 0 && LOOKAHEAD != DONE && pos < BUF_SIZE - 2)
                {
                    if (LOOKAHEAD == '(') {
                        step_strs[nbindings][pos++] = '(';
                        depth++;
                        match(pSK, '(');
                    } else if (LOOKAHEAD == ')') {
                        step_strs[nbindings][pos++] = ')';
                        depth--;
                        if (depth > 0) match(pSK, ')');
                    } else {
                        if (LOOKAHEAD == NUMBER)
                            pos += snprintf(step_strs[nbindings] + pos, BUF_SIZE - pos, "%d", pSK->yylval.ival);
                        else {
                            int len = (int)strlen(pSK->yylval.word);
                            if (pos + len + 1 < BUF_SIZE) {
                                memcpy(step_strs[nbindings] + pos, pSK->yylval.word, len);
                                pos += len;
                            }
                        }
                        step_strs[nbindings][pos++] = ' ';
                        match(pSK, LOOKAHEAD);
                    }
                }
                match(pSK, ')');
            }
            else
            {
                if (LOOKAHEAD == NUMBER)
                    pos += snprintf(step_strs[nbindings] + pos, BUF_SIZE - pos, "%d", pSK->yylval.ival);
                else {
                    int len = (int)strlen(pSK->yylval.word);
                    memcpy(step_strs[nbindings] + pos, pSK->yylval.word, len);
                    pos += len;
                }
                match(pSK, LOOKAHEAD);
            }
            step_strs[nbindings][pos] = '\0';
        }

        nbindings++;
        match(pSK, ')');
    }
    match(pSK, ')');

    // Create lambda for loop body (inlined, not using let_create_lambda)
    ParseUpvalue saved_upvalues[MAX_UPVALUES];
    int saved_upvalue_count = pSK->upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(saved_upvalues, pSK->upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);
    pSK->upvalue_count = 0;

    vm_new_environment(&pSK->vm);

    Object* proc_obj = vm_new_proc(&pSK->vm);
    vm_push(&pSK->vm, proc_obj);

    Procedure* proc = AS_OBJ(Procedure, proc_obj);
    static int do_id = 0;
    char do_name[32];
    snprintf(do_name, sizeof(do_name), "<do-%d>", do_id++);
    proc->name = (String*)vm_new_string(&pSK->vm, do_name);
    vm_define_global(&pSK->vm, (Object*)proc->name, proc_obj);

    CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

    proc->arity = nbindings;
    for (int i = 0; i < nbindings; i++)
        vm_set_var(&pSK->vm, bind_names[i], MAKE_NUM(i + 1));

    if (pSK->vm.current_scope)
    {
        Environment* env = pSK->vm.current_scope;
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
            ht_add(env->bindings, env_key, MAKE_NUM(nbindings - AS_NUM(env_value) + 3));
    }

    // Parse test clause: (test result ...)
    match(pSK, '(');

    CodeChunk* loop_chunk = vm_get_current_chunk();
    int loop_start = (int)loop_chunk->count;

    expr(pSK);
    vm_emit(&pSK->vm, OP_BNT);
    int branch_to_body = vm_emit(&pSK->vm, 0);

    if (LOOKAHEAD != ')')
    {
        expr(pSK);
        while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
        {
            vm_emit(&pSK->vm, OP_POP);
            expr(pSK);
        }
    }
    else
    {
        vm_emit_push_object(&pSK->vm, NULL);
    }
    match(pSK, ')');

    vm_emit(&pSK->vm, OP_RET);

    loop_chunk->code[branch_to_body] = loop_chunk->count - branch_to_body;

    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        expr(pSK);
        vm_emit(&pSK->vm, OP_POP);
    }

    // Compile step expressions: evaluate all first, then assign all (R5RS parallel update)
    // First pass: compile all step value expressions (push results onto stack)
    int has_step[MAX_DO_BINDINGS];
    for (int i = 0; i < nbindings; i++)
    {
        has_step[i] = (step_strs[i][0] != '\0');
        if (has_step[i])
        {
            const char* saved_eval = pSK->eval_input;
            int saved_eval_pos = pSK->eval_pos;
            int saved_eval_depth = pSK->eval_file_depth;
            int saved_lookahead = pSK->lookahead;
            int saved_expr_level = pSK->expr_level;

            pSK->eval_input = step_strs[i];
            pSK->eval_pos = 0;
            pSK->eval_file_depth = pSK->file_stack_ptr;
            pSK->expr_level = 1;
            pSK->lookahead = yylex(pSK);
            expr(pSK);

            pSK->eval_input = saved_eval;
            pSK->eval_pos = saved_eval_pos;
            pSK->eval_file_depth = saved_eval_depth;
            pSK->lookahead = saved_lookahead;
            pSK->expr_level = saved_expr_level;
        }
    }
    // Second pass: assign in reverse order (last pushed = last binding)
    for (int i = nbindings - 1; i >= 0; i--)
    {
        if (has_step[i])
        {
            vm_emit(&pSK->vm, OP_SET_LOCAL);
            vm_emit(&pSK->vm, (VMCode_t)(nbindings - i + 2));
        }
    }

    // Jump back to loop start
    vm_emit(&pSK->vm, OP_BRA);
    int bra_offset_pos = (int)loop_chunk->count;  // position where offset will be stored
    vm_emit(&pSK->vm, (VMCode_t)(loop_start - bra_offset_pos));

    vm_set_current_chunk(&pSK->vm, prev_chunk);
    vm_free_environment(&pSK->vm);

    // Handle closure capture
    int num_upvalues = pSK->upvalue_count;
    ParseUpvalue captured[MAX_UPVALUES];
    if (num_upvalues > 0)
        memcpy(captured, pSK->upvalues, sizeof(ParseUpvalue) * num_upvalues);

    // Restore outer upvalues
    pSK->upvalue_count = saved_upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(pSK->upvalues, saved_upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);

    if (num_upvalues > 0)
    {
        for (int i = 0; i < num_upvalues; i++)
        {
            vm_emit(&pSK->vm, captured[i].is_upvalue ? OP_CAPTURE_UPVALUE : OP_CAPTURE_LOCAL);
            vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
        }
        vm_emit_push_object(&pSK->vm, proc_obj);
        vm_emit(&pSK->vm, OP_MAKE_CLOSURE);
        vm_emit(&pSK->vm, (VMCode_t)num_upvalues);
    }
    else
    {
        vm_emit_push_object(&pSK->vm, proc_obj);
    }

    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)nbindings);

    unindent(pSK);
    return 0;
}

// Returns proc_obj. Caller must have already emitted value code.
// Stack after: [...vals..., proc/closure]
//-----------------------------------------------
static Object* let_create_lambda(SKState* pSK, char bind_names[][64], int nbindings, BodyPrologue prologue)
{
    ParseUpvalue saved_upvalues[MAX_UPVALUES];
    int saved_upvalue_count = pSK->upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(saved_upvalues, pSK->upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);
    pSK->upvalue_count = 0;

    vm_new_environment(&pSK->vm);

    Object* proc_obj = vm_new_proc(&pSK->vm);
    vm_push(&pSK->vm, proc_obj);

    Procedure* proc = AS_OBJ(Procedure, proc_obj);
    static int let_id = 0;
    char let_name[32];
    snprintf(let_name, sizeof(let_name), "<let-%d>", let_id++);
    proc->name = (String*)vm_new_string(&pSK->vm, let_name);
    vm_define_global(&pSK->vm, (Object*)proc->name, proc_obj);

    CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

    // Register parameter names
    proc->arity = nbindings;
    for (int i = 0; i < nbindings; i++)
        vm_set_var(&pSK->vm, bind_names[i], MAKE_NUM(i + 1));

    // Fix up frame offsets
    if (pSK->vm.current_scope)
    {
        Environment* env = pSK->vm.current_scope;
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
            ht_add(env->bindings, env_key, MAKE_NUM(nbindings - AS_NUM(env_value) + 3));
    }

    // Parse body
    local_body_ex(pSK, prologue);

    vm_emit(&pSK->vm, OP_RET);
    vm_set_current_chunk(&pSK->vm, prev_chunk);
    vm_free_environment(&pSK->vm);

    // Handle closure capture
    int num_upvalues = pSK->upvalue_count;
    ParseUpvalue captured[MAX_UPVALUES];
    if (num_upvalues > 0)
        memcpy(captured, pSK->upvalues, sizeof(ParseUpvalue) * num_upvalues);

    pSK->upvalue_count = saved_upvalue_count;
    if (saved_upvalue_count > 0)
        memcpy(pSK->upvalues, saved_upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);

    for (int i = 0; i < num_upvalues; i++)
    {
        if (captured[i].scope_depth > 1)
        {
            int parent_uv_idx = register_upvalue(pSK, captured[i].name,
                captured[i].scope_depth - 1, captured[i].local_index);
            captured[i].local_index = parent_uv_idx;
            captured[i].is_upvalue = 1;
        }
    }

    if (num_upvalues > 0)
    {
        for (int i = 0; i < num_upvalues; i++)
        {
            vm_emit(&pSK->vm, captured[i].is_upvalue ? OP_CAPTURE_UPVALUE : OP_CAPTURE_LOCAL);
            vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
        }
        vm_emit_push_object(&pSK->vm, proc_obj);
        vm_emit(&pSK->vm, OP_MAKE_CLOSURE);
        vm_emit(&pSK->vm, (VMCode_t)num_upvalues);
    }
    else
    {
        vm_emit_push_object(&pSK->vm, proc_obj);
    }

    return proc_obj;
}

//-----------------------------------------------
// Parse let bindings: ((var val) ...) — emits values, collects names
//-----------------------------------------------
#define MAX_LET_BINDINGS 32

static int let_parse_bindings(SKState* pSK, char bind_names[][64])
{
    if (LOOKAHEAD == NIL)
    {
        match(pSK, NIL);    // "()" lexes as one token: no bindings
        return 0;
    }

    match(pSK, '(');

    int nbindings = 0;
    while (LOOKAHEAD == '(')
    {
        match(pSK, '(');
        if (nbindings >= MAX_LET_BINDINGS)
            yyerror(pSK, "too many let bindings");

        strncpy(bind_names[nbindings], pSK->yylval.word, 63);
        bind_names[nbindings][63] = '\0';
        nbindings++;

        match(pSK, ID);
        expr(pSK);      // emit value code into current chunk
        match(pSK, ')');
    }

    match(pSK, ')');
    return nbindings;
}

//-----------------------------------------------
// let_expr: (let ((var val) ...) body ...)
// Desugars to ((lambda (var ...) body ...) val ...)
//-----------------------------------------------
static void let_star_rest(SKState* pSK);

//-----------------------------------------------
// One let* binding, with LOOKAHEAD at its '(': evaluate the value in the
// current scope, then call a one-parameter lambda whose body holds the
// remaining bindings and finally the let* body (see let_star_rest).
//-----------------------------------------------
static void let_star_binding(SKState* pSK)
{
    char bind_name[1][64];

    match(pSK, '(');
    strncpy(bind_name[0], pSK->yylval.word, 63);
    bind_name[0][63] = '\0';
    match(pSK, ID);
    expr(pSK);                  // value, which can see the earlier bindings
    match(pSK, ')');

    let_create_lambda(pSK, bind_name, 1, let_star_rest);

    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;

    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)1);
}

//-----------------------------------------------
// Body prologue of a let* lambda: the next binding (which nests the rest),
// or, after the last one, the end of the binding list. The innermost lambda
// then parses the let* body itself.
//-----------------------------------------------
static void let_star_rest(SKState* pSK)
{
    if (LOOKAHEAD == ')')
        match(pSK, ')');        // end of the bindings: the body follows
    else
        let_star_binding(pSK);
}

//-----------------------------------------------
// let_star_expr: (let* ((var val) ...) body ...)
// Each binding sees the ones before it, so this desugars to nested lets:
// (let ((var1 val1)) (let* ((var2 val2) ...) body ...))
//-----------------------------------------------
static int let_star_expr(SKState* pSK)
{
    match(pSK, LET_STAR);

    if (LOOKAHEAD == NIL)
    {
        // (let* () body ...) is (let () body ...); "()" lexes as one token
        match(pSK, NIL);
        let_create_lambda(pSK, NULL, 0, NULL);

        CodeChunk* chunk = vm_get_current_chunk();
        pSK->last_call_offset = (int)chunk->count;
        pSK->last_call_chunk = chunk;

        vm_emit(&pSK->vm, OP_ICALL);
        vm_emit(&pSK->vm, (VMCode_t)0);
    }
    else
    {
        match(pSK, '(');        // the binding list
        let_star_binding(pSK);
    }

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// Record a token just read (with its yylval) for replay
//-----------------------------------------------
static void replay_record(SKState* pSK, TokenReplay* replay, int* capacity, int token)
{
    if (replay->count == *capacity)
    {
        *capacity = *capacity ? *capacity * 2 : 32;
        replay->tokens = (int*)realloc(replay->tokens, sizeof(int) * *capacity);
        replay->values = (YYLVAL*)realloc(replay->values, sizeof(YYLVAL) * *capacity);
    }
    replay->tokens[replay->count] = token;
    replay->values[replay->count] = pSK->yylval;
    replay->count++;
}

//-----------------------------------------------
// Replay the recorded tokens, which began at LOOKAHEAD: the lexer hands
// them out again, counted afresh from 'depth_before', the paren depth
// before the first of them
//-----------------------------------------------
static void replay_start(SKState* pSK, TokenReplay* replay, int depth_before)
{
    pSK->paren_depth = depth_before;
    replay->next = pSK->replay;
    pSK->replay = replay;
    LOOKAHEAD = yylex(pSK);
}

//-----------------------------------------------
// Internal definitions. A body that starts with defines,
//   (define (f x) (define a 1) (define (g y) (+ a y)) (g x))
// compiles like letrec* over the rest of the body,
//   ((lambda (a g) (set! a 1) (set! g (lambda (y) (+ a y))) (g x)) <unspecified> <unspecified>)
// so the names are local to each call of f, and each definition can use
// the ones before it. As in letrec, the defines are read ahead to collect
// the names, then replayed to compile them.
//
// Returns 1 if the body started with defines, having compiled the whole
// rest of the body; 0, with nothing compiled, if not.
//-----------------------------------------------
static int body_define_count;

static void body_define_inits(SKState* pSK)
{
    int ndefs = body_define_count;

    for (int i = 0; i < ndefs; i++)
    {
        match(pSK, '(');
        match(pSK, DEFINE);

        int is_proc = (LOOKAHEAD == '(');
        if (is_proc)
            match(pSK, '(');

        char name[64];
        strncpy(name, pSK->yylval.word, 63);
        name[63] = '\0';

        int scope_depth = -1;
        Object* slot = vm_get_local_var_ex(&pSK->vm, name, &scope_depth);
        match(pSK, ID);

        if (is_proc)
            lambda_ex(pSK, name);       // (define (name param ...) body ...)
        else
            expr(pSK);                  // (define name value)

        vm_emit(&pSK->vm, OP_SET_LOCAL);
        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(slot));

        match(pSK, ')');
    }
}

static int body_defines(SKState* pSK)
{
    if (LOOKAHEAD != '(')
        return 0;

    // record the leading define forms, and the two tokens after them that
    // show the next form is not a define
    TokenReplay* replay = (TokenReplay*)calloc(1, sizeof(TokenReplay));
    int capacity = 0;
    int depth_before = pSK->paren_depth - token_depth(LOOKAHEAD);
    int token = LOOKAHEAD;

    char names[MAX_LET_BINDINGS][64];
    int ndefs = 0;

    while (token == '(')
    {
        replay_record(pSK, replay, &capacity, token);
        token = yylex(pSK);
        if (token != DEFINE)
            break;

        // (define name ...) or (define (name ...) ...)
        replay_record(pSK, replay, &capacity, token);
        int depth = 1;
        token = yylex(pSK);
        if (token == '(')
        {
            replay_record(pSK, replay, &capacity, token);
            depth++;
            token = yylex(pSK);
        }

        if (token == ID)
        {
            if (ndefs >= MAX_LET_BINDINGS)
                yyerror(pSK, "too many internal defines");
            strncpy(names[ndefs], pSK->yylval.word, 63);
            names[ndefs][63] = '\0';
            ndefs++;
        }

        // the rest of the define form, through its ')'
        while (depth > 0 && token != DONE)
        {
            replay_record(pSK, replay, &capacity, token);
            depth += token_depth(token);
            token = yylex(pSK);
        }
    }
    replay_record(pSK, replay, &capacity, token);
    replay_start(pSK, replay, depth_before);

    if (ndefs == 0)
        return 0;

    // every name starts out unspecified
    for (int i = 0; i < ndefs; i++)
        vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());

    body_define_count = ndefs;
    let_create_lambda(pSK, names, ndefs, body_define_inits);

    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;

    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)ndefs);

    return 1;
}

//-----------------------------------------------
// Body prologue of the letrec lambda: replay the binding list and assign
// each initializer to its variable (a parameter of the lambda) in order.
// The initializers are compiled inside the lambda, so they can refer to
// every letrec variable; closures capture the variables themselves, so
// they see the values assigned after they were created.
//-----------------------------------------------
static void letrec_inits(SKState* pSK)
{
    match(pSK, '(');
    while (LOOKAHEAD == '(')
    {
        match(pSK, '(');

        int scope_depth = -1;
        Object* slot = vm_get_local_var_ex(&pSK->vm, pSK->yylval.word, &scope_depth);
        match(pSK, ID);

        expr(pSK);                              // initializer
        vm_emit(&pSK->vm, OP_SET_LOCAL);
        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(slot));

        match(pSK, ')');
    }
    match(pSK, ')');
}

//-----------------------------------------------
// letrec_expr: (letrec ((var init) ...) body ...)
// Desugars to ((lambda (var ...) (set! var init) ... body ...) <unspecified> ...)
//
// The variables must all be declared before any initializer is compiled
// (they may be mutually recursive), but the compiler is single pass. So
// the binding list is read ahead once to collect the names, then replayed
// to compile it.
//-----------------------------------------------
static int letrec_expr(SKState* pSK)
{
    match(pSK, LETREC);

    char bind_names[MAX_LET_BINDINGS][64];
    int nbindings = 0;

    if (LOOKAHEAD == NIL)
    {
        match(pSK, NIL);    // (letrec () body ...): no bindings, nothing to replay
    }
    else
    {
        // record the binding list, from LOOKAHEAD's '(' through its ')'
        TokenReplay* replay = (TokenReplay*)calloc(1, sizeof(TokenReplay));
        int capacity = 0, depth = 0, prev = 0;
        int depth_before = pSK->paren_depth - token_depth(LOOKAHEAD);
        int token = LOOKAHEAD;

        for (;;)
        {
            replay_record(pSK, replay, &capacity, token);

            depth += token_depth(token);

            // a binding's name is the ID right after its '(', at depth 2
            if (token == ID && prev == '(' && depth == 2 && nbindings < MAX_LET_BINDINGS)
            {
                strncpy(bind_names[nbindings], pSK->yylval.word, 63);
                bind_names[nbindings][63] = '\0';
                nbindings++;
            }

            if (depth <= 0 || token == DONE)
                break;

            prev = token;
            token = yylex(pSK);
        }

        replay_start(pSK, replay, depth_before);
    }

    // initial values: every variable starts out unspecified
    for (int i = 0; i < nbindings; i++)
        vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());

    let_create_lambda(pSK, bind_names, nbindings, nbindings ? letrec_inits : NULL);

    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;

    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)nbindings);

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// Named let: (let name ((var init) ...) body ...) binds name, within the
// body, to a procedure of the vars whose body is the let body, and calls it
// with the inits. It compiles like
//   ((lambda (<arg0> ... name)                 ; inits, and name unspecified
//      (set! name (lambda (var ...) body ...))
//      (name <arg0> ...))
//    init ... <unspecified>)
// <argN> cannot be written in source (identifiers do not start with '<'),
// so the body cannot see them. The prologue of the outer lambda does the
// set! and the first call, so it needs the binding names: named_let_info
// passes them, read as soon as the prologue starts.
//-----------------------------------------------
typedef struct NamedLet
{
    char loop_name[64];
    char names[MAX_LET_BINDINGS][64];
    int nbindings;
} NamedLet;

static NamedLet* named_let_info;

static void named_let_prologue(SKState* pSK)
{
    NamedLet* nl = named_let_info;

    // the loop procedure, from the let body; it captures name itself
    let_create_lambda(pSK, nl->names, nl->nbindings, NULL);
    int scope_depth = -1;
    Object* name_slot = vm_get_local_var_ex(&pSK->vm, nl->loop_name, &scope_depth);
    vm_emit(&pSK->vm, OP_SET_LOCAL);
    vm_emit(&pSK->vm, (VMCode_t)AS_NUM(name_slot));

    // the first call: (name <arg0> ...)
    for (int i = 0; i < nl->nbindings; i++)
    {
        char arg_name[64];
        snprintf(arg_name, sizeof(arg_name), "<arg%d>", i);
        Object* slot = vm_get_local_var_ex(&pSK->vm, arg_name, &scope_depth);
        vm_emit(&pSK->vm, OP_GET_LOCAL);
        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(slot));
    }
    vm_emit(&pSK->vm, OP_GET_LOCAL);
    vm_emit(&pSK->vm, (VMCode_t)AS_NUM(name_slot));

    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;
    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)nl->nbindings);
}

static int named_let_expr(SKState* pSK)
{
    NamedLet nl;
    strncpy(nl.loop_name, pSK->yylval.word, 63);
    nl.loop_name[63] = '\0';
    match(pSK, ID);

    nl.nbindings = let_parse_bindings(pSK, nl.names);   // emits the inits
    vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());         // name, set in the prologue

    // parameters of the outer lambda: <arg0> ... name
    char params[MAX_LET_BINDINGS + 1][64];
    for (int i = 0; i < nl.nbindings; i++)
        snprintf(params[i], sizeof(params[i]), "<arg%d>", i);
    strcpy(params[nl.nbindings], nl.loop_name);

    named_let_info = &nl;
    let_create_lambda(pSK, params, nl.nbindings + 1, named_let_prologue);

    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;
    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)(nl.nbindings + 1));

    unindent(pSK);
    return 0;
}

static int let_expr(SKState* pSK, int token)
{
    PARSE_LOG("let_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    if (token == LET_STAR)
        return let_star_expr(pSK);

    match(pSK, token);

    if (token == LET && LOOKAHEAD == ID)
        return named_let_expr(pSK);

    char bind_names[MAX_LET_BINDINGS][64];
    int nbindings = let_parse_bindings(pSK, bind_names);

    // Create lambda, parse body, handle closures
    // Stack: [val1, ..., valN, proc/closure]
    let_create_lambda(pSK, bind_names, nbindings, NULL);

    // Call: fn on top, values underneath → ICALL
    CodeChunk* chunk = vm_get_current_chunk();
    pSK->last_call_offset = (int)chunk->count;
    pSK->last_call_chunk = chunk;

    vm_emit(&pSK->vm, OP_ICALL);
    vm_emit(&pSK->vm, (VMCode_t)nbindings);

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

    // Check if var is local (with scope depth)
    int scope_depth = -1;
    Object* obj = vm_get_local_var_ex(&pSK->vm, pSK->yylval.word, &scope_depth);

    char var_name[BUF_SIZE];
    strncpy(var_name, pSK->yylval.word, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';

    match(pSK, ID);

    if (scope_depth == 0 && IS_NUM(obj))
    {
        // Local variable in current scope
        expr(pSK);
        vm_emit(&pSK->vm, OP_SET_LOCAL);
        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(obj));
        vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());   // set! has no useful value
    }
    else if (scope_depth > 0 && IS_NUM(obj))
    {
        // Variable from enclosing scope — set via upvalue
        int uv_idx = register_upvalue(pSK, var_name, scope_depth, AS_NUM(obj));
        expr(pSK);
        if (uv_idx >= 0) {
            vm_emit(&pSK->vm, OP_SET_UPVALUE);
            vm_emit(&pSK->vm, (VMCode_t)uv_idx);
        }
        vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());   // set! has no useful value
    }
    else
    {
        // Global variable (it may be defined later in the program)
        vm_emit_push_object(&pSK->vm, global_key(pSK, var_name));
        expr(pSK);
        vm_emit(&pSK->vm, OP_STORE);
        vm_emit_push_object(&pSK->vm, MAKE_UNSPEC());   // set! has no useful value
    }

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
    else
    {
        // No else clause - push nil/false as default value
        vm_emit(&pSK->vm, OP_FALSE);
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

    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
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
// begin_expr := '(' 'begin' expr+ ')'
// Evaluates expressions in sequence, returns last value
//-----------------------------------------------
static int begin_expr(SKState* pSK)
{
    PARSE_LOG("begin_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, BEGIN);

    if (LOOKAHEAD == ')')
    {
        // (begin) with no expressions - push nil
        vm_emit(&pSK->vm, OP_FALSE);
        unindent(pSK);
        return 0;
    }

    // Parse first expression
    expr(pSK);

    // Parse remaining expressions, discarding all but last
    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        vm_emit(&pSK->vm, OP_POP);  // discard previous value
        expr(pSK);
    }

    unindent(pSK);
    return 0;
}

//-----------------------------------------------
// case_expr := '(' 'case' expr clause+ ')'
// clause := '(' '(' datum* ')' expr ')' | '(' 'else' expr ')'
// Matches key against datums using eqv? semantics
//-----------------------------------------------
static int case_expr(SKState* pSK)
{
    PARSE_LOG("case_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);
    int end_addrs[MAX_REL_CLAUSES];
    int terms = 0;

    match(pSK, CASE_TOKEN);

    // Evaluate the key expression - stays on stack throughout
    expr(pSK);

    // Look up eqv? for datum comparison
    Object* eqv_key = vm_get_string(&pSK->vm, "eqv?");
    Object* eqv_fn = vm_get_global(&pSK->vm, eqv_key);

    if (!eqv_fn || !IS_FUNCTION(eqv_fn))
    {
        yyerror(pSK, "case: 'eqv?' not found");
        unindent(pSK);
        return 0;
    }

    // Parse clauses
    while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
    {
        match(pSK, '(');

        if (LOOKAHEAD == ELSE)
        {
            // else clause - unconditional
            match(pSK, ELSE);
            vm_emit(&pSK->vm, OP_POP);   // discard the key
            expr(pSK);                    // evaluate body
        }
        else
        {
            // datum clause: ((datum1 datum2 ...) body)
            match(pSK, '(');

            // We need to match key against any of the datums
            // Strategy: for each datum, DUP key, push datum, call eqv?
            // OR the results together - if any match, take the branch

            int match_addrs[MAX_REL_CLAUSES];
            int num_datums = 0;

            while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
            {
                vm_emit(&pSK->vm, OP_DUP);       // duplicate key for comparison
                expr(pSK);                        // push datum value
                vm_emit(&pSK->vm, OP_CALL);
                vm_emit(&pSK->vm, (VMCode_t)eqv_fn);
                vm_emit(&pSK->vm, (VMCode_t)2);   // nargs = 2

                // If match, branch to body
                vm_emit(&pSK->vm, OP_BNF);        // branch if NOT false (i.e. true)
                match_addrs[num_datums++] = vm_emit(&pSK->vm, 0);

                if (num_datums >= MAX_REL_CLAUSES) break;
            }

            match(pSK, ')');  // close datum list

            // No datum matched - skip body, jump to next clause
            vm_emit(&pSK->vm, OP_BRA);
            int skip_addr = vm_emit(&pSK->vm, 0);

            // Backpatch all datum match addresses to here (body start)
            for (int i = 0; i < num_datums; i++)
            {
                vm_get_current_chunk()->code[match_addrs[i]] = 
                    vm_get_current_chunk()->count - match_addrs[i];
            }

            // Body: pop key, evaluate body expression
            vm_emit(&pSK->vm, OP_POP);   // discard the key
            expr(pSK);                    // evaluate body

            // Jump to end of case
            vm_emit(&pSK->vm, OP_BRA);
            end_addrs[terms++] = vm_emit(&pSK->vm, 0);

            // Backpatch skip address (no match -> next clause)
            vm_get_current_chunk()->code[skip_addr] = 
                vm_get_current_chunk()->count - skip_addr;
        }

        match(pSK, ')');  // close clause
    }

    // Backpatch all end addresses
    for (int i = 0; i < terms; i++)
    {
        vm_get_current_chunk()->code[end_addrs[i]] = 
            vm_get_current_chunk()->count - end_addrs[i];
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

	// return number of arguments (caller will emit as immediate operand)
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
        vm_emit(&pSK->vm, OP_POP);       // pop the previous value

        expr(pSK);

        // The last term needs no test: its value is the value of the whole
        // expression. Emitting nothing after it also keeps a call in this
        // position in tail position.
        if (LOOKAHEAD == DONE || LOOKAHEAD == ')')
            break;

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
    int last_value_left = 0;

    match(pSK, OR);

    vm_emit_push_object(&pSK->vm, NULL);      // push a dummy nil value onto the stack

    while (LOOKAHEAD != DONE && LOOKAHEAD != ')')
    {
        vm_emit(&pSK->vm, OP_POP);       // pop the previous value

        expr(pSK);

        // The last term needs no test: its value is the value of the whole
        // expression. Emitting nothing after it also keeps a call in this
        // position in tail position.
        if (LOOKAHEAD == DONE || LOOKAHEAD == ')')
        {
            last_value_left = 1;
            break;
        }

        vm_emit(&pSK->vm, OP_DUP);       // duplicate the value for testing
        vm_emit(&pSK->vm, OP_BNF);       // if any term is not false, branch to end

        addr_list[terms++] = vm_emit(&pSK->vm, 0);  // reserve space for branch address
        if (terms >= MAX_REL_CLAUSES)
        {
            yyerror(pSK, "too many terms in 'or' expression");
            break;
        }
    }

    if (!last_value_left)
    {
        vm_emit(&pSK->vm, OP_POP);      // (or) with no terms: discard the dummy
        vm_emit(&pSK->vm, OP_FALSE);    // and yield #f
    }

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

        case LETREC:
            letrec_expr(pSK);
            break;

        case LET:
        case LET_STAR:
            let_expr(pSK, LOOKAHEAD);
            break;

        case LOAD:
            load_expr(pSK);
            break;

        case IMPORT:
            import_expr(pSK);
            break;

        case QUOTE:
            quote_expr(pSK);
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

        case BEGIN:
            begin_expr(pSK);
            break;

        case CASE_TOKEN:
            case_expr(pSK);
            break;

        case DO:
            do_expr(pSK);
            break;

        case '(':
            {
                // Subexpression in function position: ((expr) args...)
                // Evaluate the head expression (pushes callable onto stack)
                expr(pSK);

                // Parse arguments (pushed on top of fn)
                int count = expr_tail(pSK);

                // Record offset for potential tail call patching
                CodeChunk* chunk = vm_get_current_chunk();
                pSK->last_call_offset = (int)chunk->count;
                pSK->last_call_chunk = chunk;

                // Expression call — fn is under args on stack
                vm_emit(&pSK->vm, OP_XCALL);
                vm_emit(&pSK->vm, (VMCode_t)count);
            }
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
                Object* key = vm_new_string(&pSK->vm, pSK->yylval.word);

                match(pSK, LOOKAHEAD);

                {
                    int local_fn = FALSE;
                    int fn_upvalue = -1;

                    // look first for a local variable or param
                    int scope_depth = -1;
                    Object* proc = vm_get_local_var_ex(&pSK->vm, ((String*)key)->value, &scope_depth);

					// if not found, look in globals
                    if (proc && IS_NUM(proc))
                    {
                        local_fn = TRUE;

                        // a function from an enclosing scope must be read through
                        // an upvalue, not from this frame (same as variable references)
                        if (scope_depth > 0)
                            fn_upvalue = register_upvalue(pSK, ((String*)key)->value, scope_depth, AS_NUM(proc));
                    }
                    else
                        proc = vm_get_global(&pSK->vm, key);

                    // Natives are bound when the call is compiled. User procedures are
                    // looked up by name when the call runs (the last branch below), so
                    // redefining one, or defining it after this call, takes effect.
                    if (!local_fn && IS_NATIVE_FN(proc))
                    {
                        int count = expr_tail(pSK); // parse function arguments (if any)

                        // Check for specialized arithmetic opcodes (2-arg calls to known builtins)
                        if (count == 2 && IS_NATIVE_FN(proc))
                        {
                            NativeFunction* nf = (NativeFunction*)proc;
                            const char* fname = nf->name->value;
                            int spec_op = -1;
                            if (fname[0] == '+' && fname[1] == '\0') spec_op = OP_ADD;
                            else if (fname[0] == '-' && fname[1] == '\0') spec_op = OP_SUB;
                            else if (fname[0] == '*' && fname[1] == '\0') spec_op = OP_MUL;
                            else if (fname[0] == '<' && fname[1] == '=' && fname[2] == '\0') spec_op = OP_LE;
                            else if (fname[0] == '<' && fname[1] == '\0') spec_op = OP_LT;
                            else if (fname[0] == '=' && fname[1] == '\0') spec_op = OP_EQ_NUM;

                            if (spec_op >= 0)
                            {
                                // No call frame needed — emit single opcode
                                // Record offset so tail-call patching skips this (no-op patch)
                                CodeChunk* chunk = vm_get_current_chunk();
                                pSK->last_call_offset = (int)chunk->count;
                                pSK->last_call_chunk = chunk;

                                vm_emit(&pSK->vm, (VMCode_t)spec_op);
                            }
                            else
                            {
                                // Regular CALL for non-specialized native functions
                                CodeChunk* chunk = vm_get_current_chunk();
                                pSK->last_call_offset = (int)chunk->count;
                                pSK->last_call_chunk = chunk;

                                vm_emit(&pSK->vm, OP_CALL);
                                vm_emit(&pSK->vm, (VMCode_t)proc);
                                vm_emit(&pSK->vm, (VMCode_t)count);
                            }
                        }
                        else
                        {
                            // Record offset for potential tail call patching
                            CodeChunk* chunk = vm_get_current_chunk();
                            pSK->last_call_offset = (int)chunk->count;
                            pSK->last_call_chunk = chunk;
                        
                            vm_emit(&pSK->vm, OP_CALL);
                            vm_emit(&pSK->vm, (VMCode_t)proc);
                            vm_emit(&pSK->vm, (VMCode_t)count);
                        }
                    }
                    else if (local_fn)
                    {
                        int count = expr_tail(pSK); // parse function arguments (if any)

                        if (fn_upvalue >= 0)
                        {
                            vm_emit(&pSK->vm, OP_GET_UPVALUE);
                            vm_emit(&pSK->vm, (VMCode_t)fn_upvalue);
                        }
                        else
                        {
                            vm_emit(&pSK->vm, OP_GET_LOCAL);
                            vm_emit(&pSK->vm, (VMCode_t)AS_NUM(proc));
                        }

                        // Record offset for potential tail call patching
                        CodeChunk* chunk = vm_get_current_chunk();
                        pSK->last_call_offset = (int)chunk->count;
                        pSK->last_call_chunk = chunk;
                        
                        vm_emit(&pSK->vm, OP_ICALL);
                        vm_emit(&pSK->vm, (VMCode_t)count);
                    }
                    else
                    {
                        // A global user procedure, or a name not defined yet (a forward
                        // reference): look it up by name at run time. Root the name
                        // before the arguments are parsed, as parsing can trigger a GC.
                        key = global_key(pSK, ((String*)key)->value);
                        int count = expr_tail(pSK); // parse function arguments

                        emit_load_global(pSK, key);           // look up at runtime
                        
                        // Record offset for potential tail call patching
                        CodeChunk* chunk = vm_get_current_chunk();
                        pSK->last_call_offset = (int)chunk->count;
                        pSK->last_call_chunk = chunk;
                        
                        vm_emit(&pSK->vm, OP_ICALL);
                        vm_emit(&pSK->vm, (VMCode_t)count);
                    }
                }
            }
            break;
        }

        match(pSK, ')');
    }
    else
        atom(pSK);

    unindent(pSK);

    pSK->expr_level--;

    // finish a top-level expr: the REPL prints its result, a script just discards it
    if (pSK->expr_level == 0)
        vm_emit(&pSK->vm, pSK->echo_form ? OP_PRINT_TOS : OP_DISCARD_TOS);

    return 0;
}

//-----------------------------------------------
// Recover from a syntax error in a top-level form: drop the code it
// compiled to, restore the parser's top-level state, and skip the rest
// of the form so parsing resumes with the next one.
//-----------------------------------------------
static void discard_form(SKState* pSK, CodeChunk* chunk, int form_start,
                         int saved_expr_level, Environment* saved_scope)
{
    chunk->count = form_start;
    vm_set_current_chunk(&pSK->vm, chunk);
    pSK->expr_level = saved_expr_level;
    pSK->vm.current_scope = saved_scope;
    pSK->last_call_offset = -1;
    pSK->last_call_chunk = NULL;

    // parens consumed so far, not counting LOOKAHEAD, which is lexed but unparsed
    while (LOOKAHEAD != DONE && pSK->paren_depth - token_depth(LOOKAHEAD) > 0)
        LOOKAHEAD = yylex(pSK);
}

//-----------------------------------------------
// After a top-level form: a procedure define binds its name while it is
// compiled (see definition). Keep that if the form compiled; if it was
// dropped, put back the name's previous value, or leave it unbound.
//-----------------------------------------------
static void finish_define(SKState* pSK, int dropped)
{
    if (!pSK->define_name)
        return;

    Object* prev_key = vm_new_string(&pSK->vm, "<define-prev>");
    if (dropped)
        vm_define_global(&pSK->vm, pSK->define_name, vm_get_global(&pSK->vm, prev_key));
    vm_define_global(&pSK->vm, prev_key, MAKE_UNBOUND());   // don't keep the old value alive

    pSK->define_name = NULL;
}

//-----------------------------------------------
// parse the input stream (stdin or file)
//-----------------------------------------------
int yyparse(SKState *pSK)
{
    //PARSE_LOG("yyparse(): lookahead = %d '%c'\n", lookahead, (char)lookahead);

    if (pSK->yyin == NULL && pSK->eval_input == NULL)
        return 0;

    // prime the pump
    LOOKAHEAD = yylex(pSK);

    pSK->indent_level = 0;

    // the top-level is zero or more s-expressions
	while (!pSK->bDone && LOOKAHEAD != DONE)
    {
        // remember where this form starts, so a form with a syntax error can
        // be dropped rather than run half-compiled
        CodeChunk* chunk = vm_get_current_chunk();
        int form_start = (int)chunk->count;
        int errors_before = pSK->err_count;
        int saved_expr_level = pSK->expr_level;
        Environment* saved_scope = pSK->vm.current_scope;

        // LOOKAHEAD is already lexed; count nesting from the start of the form
        pSK->paren_depth = token_depth(LOOKAHEAD);

        // only forms typed at the REPL echo their value, not those from a
        // script or a loaded file
        pSK->echo_form = (pSK->eval_input == NULL && pSK->yyin == stdin);

        pSK->define_name = NULL;
        expr(pSK);

        int dropped = (pSK->err_count != errors_before);
        if (dropped)
            discard_form(pSK, chunk, form_start, saved_expr_level, saved_scope);
        finish_define(pSK, dropped);
    }

    return 0;
}

