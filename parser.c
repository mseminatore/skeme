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

    case ID:
        {
            PARSE_LOG("ID: '%s'\n", pSK->yylval.word);

            // check if var is local (with scope depth), if so emit stack read or upvalue
            int scope_depth = -1;
			Object* obj = vm_get_local_var_ex(&pSK->vm, pSK->yylval.word, &scope_depth);

            // handle variable and check for undefined symbol
            Object* key = vm_get_string(&pSK->vm, pSK->yylval.word);
            char var_name[64];
            strncpy(var_name, pSK->yylval.word, sizeof(var_name) - 1);
            var_name[sizeof(var_name) - 1] = '\0';
            match(pSK, ID);

            if (!key)
            {
                yyerror(pSK, "entity undefined");
            }
            else if (scope_depth == 0 && IS_NUM(obj))
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
		vm_set_var(&pSK->vm, pSK->yylval.word, MAKE_NUM(arity));   // add parameter to function scope
        match(pSK, ID);
    }

    if (pSK->vm.current_scope)
    {
        Environment* env = pSK->vm.current_scope;

        // update all bindings in the current environment
        // Stack layout: [args..., ret_addr, saved_fp, arity] with fp pointing past arity
        // So args are at fp-4, fp-5, etc. (arity at fp-1, saved_fp at fp-2, ret_addr at fp-3)
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
        {
			ht_add(env->bindings, env_key, MAKE_NUM(arity - AS_NUM(env_value) + 3)); // +3 for ret_addr, saved_fp, arity
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
    long p = call_offset + (op == OP_CALL ? 3 : 2);     // CALL: op,fn,nargs  ICALL: op,nargs
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

static int local_body(SKState* pSK)
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
    
    // Parse all body expressions
    while (LOOKAHEAD != ')')
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
    }
    
    // Restore state for outer function
    pSK->last_call_offset = saved_last_call_offset;
    pSK->last_call_chunk = saved_last_call_chunk;

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

        // Check if we're inside a local scope (nested define)
        int is_local = (pSK->vm.current_scope != NULL);

        // Save upvalue state for nested lambdas (like lambda() does)
        ParseUpvalue saved_upvalues[MAX_UPVALUES];
        int saved_upvalue_count = pSK->upvalue_count;
        if (is_local)
        {
            if (saved_upvalue_count > 0)
                memcpy(saved_upvalues, pSK->upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);
            pSK->upvalue_count = 0;
        }

        vm_new_environment(&pSK->vm);   // create new environment for function scope

        // function name
        Object* proc_name = vm_new_string(&pSK->vm, pSK->yylval.word);
		vm_push(&pSK->vm, proc_name);                    // stash function name on stack in case of GC
            Object* proc_obj = vm_new_proc(&pSK->vm);
		vm_pop(&pSK->vm);                                // remove function name from stack

        Procedure* proc = AS_OBJ(Procedure, proc_obj);
        proc->name = (String*)proc_name;               // set function name

        CodeChunk* prev_chunk = vm_set_current_chunk(&pSK->vm, &proc->chunk);

        // Store as GC root in globals
        if (!is_local)
        {
            // Top-level: the proc IS the final value, store under its real name
            vm_define_global(&pSK->vm, proc_name, proc_obj);
        }
        else
        {
            // Nested: store under a unique internal name for GC safety only
            // The real name will be bound via OP_STORE at runtime
            static int inner_def_id = 0;
            char gc_name[48];
            snprintf(gc_name, sizeof(gc_name), "<define-%d>", inner_def_id++);
            Object* gc_key = vm_new_string(&pSK->vm, gc_name);
            vm_define_global(&pSK->vm, gc_key, proc_obj);
        }

        match(pSK, ID);
		
        // parameters
        formals(pSK, proc);

        match(pSK, ')');
        
        // function body
        local_body(pSK);

        // return from function (arity is now stored in frame by CALL)
        vm_emit(&pSK->vm, OP_RET);
        vm_set_current_chunk(&pSK->vm, prev_chunk);

        vm_free_environment(&pSK->vm); // pop function environment off chain

        if (is_local)
        {
            // Capture upvalue info before restoring outer state
            int num_upvalues = pSK->upvalue_count;
            ParseUpvalue captured[MAX_UPVALUES];
            if (num_upvalues > 0)
                memcpy(captured, pSK->upvalues, sizeof(ParseUpvalue) * num_upvalues);

            // Restore outer lambda's upvalue state
            pSK->upvalue_count = saved_upvalue_count;
            if (saved_upvalue_count > 0)
                memcpy(pSK->upvalues, saved_upvalues, sizeof(ParseUpvalue) * saved_upvalue_count);

            // Propagate deep captures upward (same as lambda)
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
                // Emit closure creation and store to global under its real name
                vm_emit_push_object(&pSK->vm, proc_name);

                for (int i = 0; i < num_upvalues; i++)
                {
                    if (captured[i].is_upvalue)
                    {
                        vm_emit(&pSK->vm, OP_GET_UPVALUE);
                        vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
                    }
                    else
                    {
                        vm_emit(&pSK->vm, OP_GET_LOCAL);
                        vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
                    }
                }

                vm_emit_push_object(&pSK->vm, proc_obj);
                vm_emit(&pSK->vm, OP_MAKE_CLOSURE);
                vm_emit(&pSK->vm, (VMCode_t)num_upvalues);

                // Store the closure to global under its real name
                vm_emit(&pSK->vm, OP_STORE);
            }
            else
            {
                // No upvalues — store plain procedure to global under its real name
                vm_emit_push_object(&pSK->vm, proc_name);
                vm_emit_push_object(&pSK->vm, proc_obj);
                vm_emit(&pSK->vm, OP_STORE);
            }
        }
	}
    else
    {
		// variable definition
        Object* key = vm_new_string(&pSK->vm, pSK->yylval.word);
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

        // Push captured values from enclosing scope
        for (int i = 0; i < num_upvalues; i++)
        {
            if (captured[i].is_upvalue)
            {
                // Variable was captured from grandparent+ — read from parent's upvalues
                vm_emit(&pSK->vm, OP_GET_UPVALUE);
                vm_emit(&pSK->vm, (VMCode_t)captured[i].local_index);
            }
            else
            {
                // Variable is in immediate parent's locals
                vm_emit(&pSK->vm, OP_GET_LOCAL);
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
        while (LOOKAHEAD != ')' && LOOKAHEAD != DONE)
        {
            quote_datum(pSK);
            count++;
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
            int saved_lookahead = pSK->lookahead;
            int saved_expr_level = pSK->expr_level;

            pSK->eval_input = step_strs[i];
            pSK->eval_pos = 0;
            pSK->expr_level = 1;
            pSK->lookahead = yylex(pSK);
            expr(pSK);

            pSK->eval_input = saved_eval;
            pSK->eval_pos = saved_eval_pos;
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
            vm_emit(&pSK->vm, captured[i].is_upvalue ? OP_GET_UPVALUE : OP_GET_LOCAL);
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
static Object* let_create_lambda(SKState* pSK, char bind_names[][64], int nbindings)
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
    local_body(pSK);

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
            vm_emit(&pSK->vm, captured[i].is_upvalue ? OP_GET_UPVALUE : OP_GET_LOCAL);
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
static int let_expr(SKState* pSK, int token)
{
    PARSE_LOG("let_expr(): lookahead = %d '%c'\n", LOOKAHEAD, (char)LOOKAHEAD);

    match(pSK, token);

    char bind_names[MAX_LET_BINDINGS][64];
    int nbindings = let_parse_bindings(pSK, bind_names);

    // Create lambda, parse body, handle closures
    // Stack: [val1, ..., valN, proc/closure]
    let_create_lambda(pSK, bind_names, nbindings);

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

    // handle variable and check for undefined symbol
    Object* key = vm_get_string(&pSK->vm, pSK->yylval.word);
    char var_name[64];
    strncpy(var_name, pSK->yylval.word, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';

    match(pSK, ID);

    if (!key)
    {
        yyerror(pSK, "entity undefined");
        unindent(pSK);
        return 0;
    }

    if (scope_depth == 0 && IS_NUM(obj))
    {
        // Local variable in current scope
        expr(pSK);
        vm_emit(&pSK->vm, OP_SET_LOCAL);
        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(obj));
        vm_emit(&pSK->vm, OP_FALSE);  // set! returns void (nil)
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
        vm_emit(&pSK->vm, OP_FALSE);  // set! returns void (nil)
    }
    else
    {
        // Global variable
        vm_emit_push_object(&pSK->vm, key);
        expr(pSK);
        vm_emit(&pSK->vm, OP_STORE);
        vm_emit(&pSK->vm, OP_FALSE);  // set! returns void (nil)
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
    while (LOOKAHEAD != ')')
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
    while (LOOKAHEAD != ')')
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

            while (LOOKAHEAD != ')')
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
                // handle variable and check for undefined symbol
                Object* key = vm_get_string(&pSK->vm, pSK->yylval.word);
                
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

                        vm_emit(&pSK->vm, OP_GET_LOCAL);
                        vm_emit(&pSK->vm, (VMCode_t)AS_NUM(proc));
                        
                        // Record offset for potential tail call patching
                        CodeChunk* chunk = vm_get_current_chunk();
                        pSK->last_call_offset = (int)chunk->count;
                        pSK->last_call_chunk = chunk;
                        
                        vm_emit(&pSK->vm, OP_ICALL);
                        vm_emit(&pSK->vm, (VMCode_t)count);
                    }
                    else
                    {
                        // Not a known function at parse time - emit runtime lookup
                        // This handles forward references like (define f (lambda ...)) ... (f args)
                        int count = expr_tail(pSK); // parse function arguments
                        
                        vm_emit_push_object(&pSK->vm, key);  // push the name
                        vm_emit(&pSK->vm, OP_LOAD);           // look up at runtime
                        
                        // Record offset for potential tail call patching
                        CodeChunk* chunk = vm_get_current_chunk();
                        pSK->last_call_offset = (int)chunk->count;
                        pSK->last_call_chunk = chunk;
                        
                        vm_emit(&pSK->vm, OP_ICALL);
                        vm_emit(&pSK->vm, (VMCode_t)count);
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

    if (pSK->yyin == NULL && pSK->eval_input == NULL)
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

