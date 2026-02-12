//-------------------------------------
// sk.c - public API for SK project
//
// Copyright (c) 2025 Mark Seminatore
//-------------------------------------
#include "term_colors.h"
#include "sk.h"

// Global pointer to current SKState for legacy yyerror/yywarning/yyinfo functions
// These public API functions don't accept SKState* parameter, so they need this global
SKState* g_current_sk = NULL;

int sk_init_builtins(SKState* pSK);

//-----------------------------------------------
// built-in function: display
//-----------------------------------------------
int sk_display(SKState *pSK)
{
	if (!pSK) return SK_FALSE;

    // pop the top of the stack and print it
    Object *obj = vm_pop(&pSK->vm);

    FILE* stream = pSK->output_stream ? pSK->output_stream : stdout;
    fprintf(stream, TERM_BRIGHT_BLUE);
        vm_print_object(obj, VM_PRINT_PLAIN, stream);
    fprintf(stream, TERM_RESET);

    return SK_TRUE;
}

//-----------------------------------
// define a variable
//-----------------------------------
int sk_define(SKState* pSK, const char *key, Object *value)
{
    if (!pSK) return SK_FALSE;

    vm_push(&pSK->vm, value);
    vm_define_global(&pSK->vm, (Object*)vm_new_string(&pSK->vm, key), value);
    vm_pop(&pSK->vm);

    return SK_TRUE;
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
    pSK->indent_level = 0;
    pSK->bDone = 0;
    pSK->lookahead = 0;
    pSK->expr_level = 0;
    pSK->lineno = 1;
    pSK->column = 1;
    pSK->last_call_offset = -1;     // no call yet
    pSK->last_call_chunk = NULL;
//    pSK->yyin = NULL;

    // initialize error handling
    pSK->error_handler = sk_default_error_handler;
    pSK->error_stream = stderr;
    pSK->output_stream = stdout;

    // initialize sentinel entry at index 0
    pSK->file_stack[0].fptr = NULL;
    pSK->file_stack[0].filename = sk_strdup(SK_UNKNOWN_FILE);
    pSK->file_stack[0].yylineno = 0;
    pSK->file_stack[0].column = 0;

    // Set global for legacy yyerror/yywarning/yyinfo functions  
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
    vm_emit(&pSK->vm, OP_HALT);
    return result;
}

//-----------------------------------------------
// register native functions
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
