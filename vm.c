//--------------------------------------------------------------
// vm.c
//--------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "vm.h"
#include "term_colors.h"

int g_bGCLog = FALSE;
int g_bVMLog = FALSE;
int g_bProfile = FALSE;

#if defined(VM_STRESS_GC)
    int g_bGCStress = TRUE;
#else
    int g_bGCStress = FALSE;
#endif


#define GC_LOG(...) (void)(g_bGCLog && fprintf(stderr, __VA_ARGS__))
#define VM_LOG(...) (void)(g_bVMLog && fprintf(stderr, __VA_ARGS__))

// High-resolution timer for profiling
#ifdef SK_PLATFORM_WINDOWS
#include <windows.h>
static double profile_get_ms(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <time.h>
static double profile_get_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

CodeChunk* g_current_chunk = NULL;
static VM* g_vm_instance = NULL;  // set during vm_init, used by vm_print_object for custom type dispatch

//-----------------------------------------------
// print an object
//-----------------------------------------------
void vm_print_object(Object* obj, int flags, FILE* stream)
{
    if (!obj)
    {
        fprintf(stream, PRINT_NIL);   // nil
        return;
    }

    if (IS_BOOL(obj))
    {
        if (IS_TRUE(obj))
            fprintf(stream, PRINT_TRUE);
        else
            fprintf(stream, PRINT_FALSE);

        return;
    }

    // Handle tagged characters
    if (IS_CHAR(obj))
    {
        char c = AS_CHAR(obj);
        switch (c)
        {
        case '\n':   fprintf(stream, "#\\newline"); break;
        case '\r':   fprintf(stream, "#\\return"); break;
        case '\t':   fprintf(stream, "#\\tab"); break;
        case ' ':    fprintf(stream, "#\\space"); break;
        case '\x1b': fprintf(stream, "#\\escape"); break;
        default:     fprintf(stream, "#\\%c", c); break;
        }
        return;
    }

#if SK_64BIT
    // Handle tagged floats (64-bit only)
    if (IS_TAGGED_FLOAT(obj))
    {
        fprintf(stream, "%g", AS_TAGGED_FLOAT(obj));
        return;
    }
#endif

    if (IS_OBJ(obj))
    {
        switch (OBJ_TYPE(obj))
        {
        case OBJ_STRING:
        {
            String* strObj = (String*)obj;
            if (VM_PRINT_PLAIN == flags)
                fprintf(stream, "%s", strObj->value);
            else
                fprintf(stream, "\"%s\"", strObj->value);
        }
        break;

        case OBJ_PAIR:
        {
            Pair* pair = (Pair*)obj;
            if (VM_PRINT_PLAIN == flags)
                fprintf(stream, "(");
            else
                fprintf(stream, "'(");

            vm_print_object(pair->car, flags, stream); // display car

            Object* cdr = pair->cdr;
            while (cdr && IS_OBJ(cdr) && OBJ_TYPE(cdr) == OBJ_PAIR)
            {
                pair = (Pair*)cdr;
                fprintf(stream, " ");
                vm_print_object(pair->car, flags, stream); // display car
                cdr = pair->cdr;
            }

            if (cdr && !IS_NIL(cdr))
            {
                fprintf(stream, " . ");
                vm_print_object(pair->cdr, flags, stream); // display cdr
            }
            fprintf(stream, ")");
        }
        break;

        case OBJ_PROCEDURE:
            fprintf(stream, "#<procedure:%s>", ((String*)((Procedure*)obj)->name)->value);
            break;

        case OBJ_NATIVE_FN:
            fprintf(stream, "#<native_fn:%s>", ((String*)((NativeFunction*)obj)->name)->value);
            break;

        case OBJ_PORT:
        {
            char* str = "<null>";
            Port* port = (Port*)obj;
            if (port && port->fptr && port->name)
                str = (char*)port->name->value;

            fprintf(stream, "#<file:'%s'>", str);
        }
			break;

        case OBJ_FLOAT:
        {
            Float* flt = (Float*)obj;
            fprintf(stream, "%g", flt->value);
        }
        break;

        default:
        {
            // Dispatch to custom type descriptor if registered
            const SKTypeDesc* desc = g_vm_instance ? vm_get_type_desc(g_vm_instance, OBJ_TYPE(obj)) : NULL;
            if (desc && desc->print_fn)
                desc->print_fn(obj, flags, stream);
            else if (desc)
                fprintf(stream, "#<%s>", desc->name);
            else
                fprintf(stream, "#<obj type=%d>", OBJ_TYPE(obj));
        }
            break;
        }
    }
    else
    {
        // print number (tagged integer)
        fprintf(stream, "%d", AS_NUM(obj));
    }
}

//--------------------------------------
// Get the current code chunk
//--------------------------------------
CodeChunk* vm_get_current_chunk()
{
	return g_current_chunk;
}

//--------------------------------------
// Set the current code chunk for bytecode generation
// Returns the previous chunk, or NULL if the input chunk is NULL (error)
// Note: This function is defensive - callers always pass valid chunk addresses
// like &proc->chunk, so the null check should never trigger in normal operation
//--------------------------------------
CodeChunk *vm_set_current_chunk(VM* pvm, CodeChunk* chunk)
{
    VM_LOG("vm: set current chunk = %p", chunk);

    if (!chunk)
    {
        yyerror(pvm->sk_state, "vm_set_current_chunk: null chunk pointer");
        return NULL;
    }

    CodeChunk* previous_chunk = g_current_chunk;
    g_current_chunk = chunk;
    return previous_chunk;
}

//--------------------------------------
// case sensistive string compare
//--------------------------------------
static int compare_fn(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

//--------------------------------------------------------------
// Initialize the VM
//--------------------------------------------------------------
int vm_init(VM* pvm) {
    CHECK_THAT_MSG(pvm->sk_state, pvm != NULL, "vm_init: null VM pointer");

    pvm->sp = 0;
    pvm->fp = 0;
    pvm->ip = 0;
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->objectsCollected = 0;
    pvm->nextGC = VM_INITIAL_GC_THRESHOLD; // initial threshold
    pvm->allocatedBytes = 0;
    pvm->gc_cycles = 0;

	pvm->max_call_depth = 0;
	pvm->max_stack_size = 0;

    // clear stack
    //memset(pvm->stack, 0, sizeof(pvm->stack));

	// init globals - since we use pointer keys, use default hash and compare funcs
    pvm->global_table = ht_create();

	// init interned strings table
    pvm->interned_strings = ht_create();
    ht_set_hash_func(pvm->interned_strings, HT_HASH_STRING);
    ht_set_compare_func(pvm->interned_strings, compare_fn);

    // initialize current scope to NULL (ie. global scope)
    pvm->current_scope = NULL;
    pvm->current_closure = NULL;

    //pvm->vm_code_ptr = 0;
    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;

    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->grayStack = NULL;

	// TODO - pair object pool init
#ifdef VM_USE_FREE_LIST
    pvm->freeObjects = 0;
#endif

    // Init extensible type registry
    pvm->custom_types = NULL;
    pvm->num_custom_types = 0;
    pvm->custom_types_capacity = 0;

    g_vm_instance = pvm;

    return VM_OK;
}

//--------------------------------------------------------------
// Shutdown the VM and free all resources
//--------------------------------------------------------------
int vm_shutdown(VM* pvm)
{
    CHECK_THAT_MSG(pvm->sk_state, pvm != NULL, "vm_shutdown: null VM pointer");

    vm_freeObjects(pvm);

    free(pvm->grayStack);
    pvm->grayStack = NULL;

	ht_free(pvm->global_table);
	pvm->global_table = NULL;

    ht_free(pvm->interned_strings);
	pvm->interned_strings = NULL;

    // Free extensible type registry
    if (pvm->custom_types) {
        free(pvm->custom_types);
        pvm->custom_types = NULL;
        pvm->num_custom_types = 0;
        pvm->custom_types_capacity = 0;
    }

    return VM_OK;
}

//--------------------------------------------------------------
// Register a custom object type
// Returns type ID >= OBJ_LAST on success, -1 on failure
//--------------------------------------------------------------
int vm_register_type(VM* pvm, const SKTypeDesc* desc)
{
    if (!pvm || !desc || !desc->name) return -1;

    // Grow array if needed
    if (pvm->num_custom_types >= pvm->custom_types_capacity) {
        int new_cap = pvm->custom_types_capacity < 4 ? 4 : pvm->custom_types_capacity * 2;
        SKTypeDesc* new_arr = (SKTypeDesc*)realloc(pvm->custom_types, sizeof(SKTypeDesc) * new_cap);
        if (!new_arr) return -1;
        pvm->custom_types = new_arr;
        pvm->custom_types_capacity = new_cap;
    }

    int type_id = OBJ_LAST + pvm->num_custom_types;
    pvm->custom_types[pvm->num_custom_types] = *desc;
    pvm->num_custom_types++;

    return type_id;
}

//--------------------------------------------------------------
// Look up type descriptor for a custom type
//--------------------------------------------------------------
const SKTypeDesc* vm_get_type_desc(VM* pvm, int type_id)
{
    if (!pvm || type_id < OBJ_LAST) return NULL;
    int idx = type_id - OBJ_LAST;
    if (idx >= pvm->num_custom_types) return NULL;
    return &pvm->custom_types[idx];
}

//--------------------------------------------------------------
// Clear the VM stack
//--------------------------------------------------------------
int vm_clear_stack(VM* pvm)
{
    CHECK_THAT_MSG(pvm->sk_state, pvm != NULL, "vm_clear_stack: null VM pointer");

    pvm->sp = 0;

    return VM_OK;
}

//--------------------------------------------------------------
// Get the current instruction pointer in the current code chunk
//--------------------------------------------------------------
VMCode_t* vm_get_chunk_ip()
{
	return &(g_current_chunk->code[g_current_chunk->count-1]);
}

//--------------------------------------------------------------
// Emit a bytecode instruction
//--------------------------------------------------------------
int vm_emit(VM* pvm, VMCode_t opcode)
{
    if (g_current_chunk == NULL) {
		fprintf(stderr, "No current code chunk set for VM emit!\n");
		exit(1);
	}

    CodeChunk* chunk = g_current_chunk;

    // grow chunk if necessary
	if (chunk->capacity < chunk->count + 1) {
		size_t old_capacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(old_capacity);
		chunk->code = (VMCode_t*)vm_alloc(pvm, chunk->code, old_capacity * sizeof(VMCode_t), chunk->capacity * sizeof(VMCode_t));
	}

	chunk->code[chunk->count++] = opcode;
    return (int)chunk->count-1;

	// TODO - line number tracking
	/*
	if (chunk->lines.capacity < chunk->count + 1) {
		int old_capacity = chunk->lines.capacity;
		chunk->lines.capacity = GROW_CAPACITY(old_capacity);
		chunk->lines.data = (int*)realloc(chunk->lines.data, sizeof(int) * chunk->lines.capacity);
	}
	chunk->lines.data[chunk->count - 1] = line;
	*/
}

//--------------------------------------------------------------
// Emit a numeric value as bytecode
//--------------------------------------------------------------
void vm_emit_number(VM* pvm, int val)
{
    vm_emit(pvm, OP_NUM);
    vm_emit(pvm, (VMCode_t)val);
}

//--------------------------------------------------------------
// Emit a string value as bytecode
//--------------------------------------------------------------
void vm_emit_string(VM* pvm, const char* str)
{
    vm_emit(pvm, OP_STRING);
    vm_emit(pvm, (VMCode_t)str);
}

//--------------------------------------------------------------
// Emit an object as bytecode
//--------------------------------------------------------------
void vm_emit_push_object(VM* pvm, Object* obj)
{
    vm_emit(pvm, OP_PUSH);
    vm_emit(pvm, (VMCode_t)obj);
}

//--------------------------------------------------------------
// Clear the VM code buffer
//--------------------------------------------------------------
void vm_clear_code(VM* pvm)
{
    g_current_chunk->count = 0;
}

//--------------------------------------------------------------
// Reset the VM
//--------------------------------------------------------------
int vm_reset(VM* pvm)
{
    CHECK_THAT_MSG(pvm->sk_state, pvm != NULL, "vm_reset: null VM pointer");

    vm_clear_stack(pvm);
    vm_clear_code(pvm);
    
    return VM_OK;
}

//---------------------------------------------------------------
// Disassemble a code chunk for debugging
//---------------------------------------------------------------
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name)
{
    printf("== Disassembly of function '%s' ==\n", name);

    VMCode_t* ip = chunk->code;

    for (; ip < &chunk->code[chunk->count];)
    {
        printf("%p ", ip);
        VMCode_t instruction = *ip++;

        printf(TERM_BRIGHT_BLUE);

        switch (instruction)
        {
        case OP_PUSH:
            printf("PUSH %p\n", (VMCode_t*)*ip++);
            break;

        case OP_POP:
            printf("POP\n");
            break;

        case OP_PUSH_IP:
            printf("PUSH IP\n");
            break;

        case OP_POP_IP:
            printf("POP IP\n");
            break;

        case OP_PUSH_FP:
            printf("PUSH FP\n");
            break;

        case OP_POP_FP:
            printf("POP FP\n");
            break;

        case OP_PUSH_SP:
            printf("PUSH SP\n");
            break;

        case OP_POP_SP:
            printf("POP SP\n");
            break;

        case OP_NUM:
            printf("PUSH NUM %zd\n", *ip++);
            break;

        case OP_STRING:
            printf("STRING '%s'\n", (const char*)*ip++);
            break;

        case OP_TRUE:
            printf("PUSH TRUE\n");
            break;

        case OP_FALSE:
            printf("PUSH FALSE\n");
            break;

        case OP_STORE:
            printf("STORE\n");
            break;

        case OP_LOAD:
            printf("LOAD\n");
            break;

        case OP_DUP:
            printf("DUP\n");
            break;

        case OP_RET:
            printf("RET\n");
            break;

        case OP_ICALL:
            printf("ICALL nargs=%zd\n", *ip++);
            break;

        case OP_CALL:
        {
            Object* fn_obj = (Object*)*ip++;
            int nargs = (int)*ip++;

            if (IS_PROCEDURE(fn_obj))
            {
                Procedure* proc = (Procedure*)fn_obj;
                printf("CALL '%s' %p nargs=%d\n", proc->name->value, proc->chunk.code, nargs);
            }
            else
            {
                NativeFunction* proc = (NativeFunction*)fn_obj;
                printf("CALL NATIVE FN '%s' nargs=%d\n", proc->name->value, nargs);
            }
        }
            break;

        case OP_NOP:
            printf("NOP\n");
			break;

        case OP_BRA:
            printf("BRA %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_BNT:
            printf("BNT %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_BNF:
            printf("BNF %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_HALT:
            printf("HALT\n");
            break;

        case OP_LIST:
            printf("MAKE_LIST count=%zd\n", *ip++);
            break;

        // case OP_PROC:
        //     printf("PROC\n");
        //     break;

        case OP_MAKE_CLOSURE:
            printf("MAKE_CLOSURE upvalues=%zd\n", *ip++);
            break;

        case OP_GET_UPVALUE:
            printf("GET_UPVALUE idx=%zd\n", *ip++);
            break;

        case OP_SET_UPVALUE:
            printf("SET_UPVALUE idx=%zd\n", *ip++);
            break;

        case OP_TAILCALL:
        {
            Object* fn_obj = (Object*)*ip++;
            int nargs = (int)*ip++;

            if (IS_PROCEDURE(fn_obj))
            {
                Procedure* proc = (Procedure*)fn_obj;
                printf("TAILCALL '%s' %p nargs=%d\n", proc->name->value, proc->chunk.code, nargs);
            }
            else
            {
                NativeFunction* proc = (NativeFunction*)fn_obj;
                printf("TAILCALL NATIVE FN '%s' nargs=%d\n", proc->name->value, nargs);
            }
        }
            break;

        case OP_TAILCALL_I:
            printf("TAILCALL_I nargs=%zd\n", *ip++);
            break;

        case OP_GET_LOCAL:
            printf("GET_LOCAL idx=%zd\n", *ip++);
            break;
            
        case OP_SET_LOCAL:
            printf("SET_LOCAL\n");
            break;

        case OP_PRINT_TOS:
            printf("PRINT_TOS\n");
			break;

        default:
            printf("Unknown opcode: %zd\n", instruction);
            break;
        }

        printf(TERM_RESET);
    }
}

static void vm_call(VM* pvm, VMCode_t* addr)
{
    // push return address onto return stack
    vm_push_ip(pvm, pvm->ip);

    // set IP to function address
    pvm->ip = addr;
}

static void vm_return(VM* pvm)
{
    // set IP to return address
    pvm->ip = vm_pop_ip(pvm);
}

static void vm_push_frame(VM* pvm)
{
    // push current frame pointer onto stack
    vm_push(pvm, MAKE_NUM(pvm->fp));

    // set new frame pointer to current stack pointer
    pvm->fp = pvm->sp;
}

static void vm_pop_frame(VM* pvm)
{
    // pop old frame pointer from stack
    Object* fp_obj = vm_pop(pvm);
    pvm->fp = AS_NUM(fp_obj);
}

static void vm_call_procedure(VM* pvm, Object* fn_obj, int nargs, const char* call_type)
{
    if (IS_CLOSURE(fn_obj))
    {
        Closure* closure = (Closure*)fn_obj;
        Procedure* proc = closure->function;
        VM_LOG("%s CLOSURE '%s' %p\n", call_type, proc->name->value, proc->chunk.code);

        if (g_bProfile) proc->call_count++;

        if (proc->arity != nargs)
        {
            yyerror(pvm->sk_state, "incorrect number of arguments to closure");
            vm_pop_n(pvm, nargs);
            pvm->ip++;
            return;
        }

        vm_push_ip(pvm, pvm->ip);               // push return address
        pvm->ip = proc->chunk.code;             // set IP to function code
        vm_push(pvm, MAKE_NUM(pvm->fp));        // push old frame pointer
        vm_push(pvm, MAKE_NUM(proc->arity));    // push arity
        pvm->fp = pvm->sp;
        pvm->current_closure = closure;         // set current closure for upvalue access
    }
    else if (IS_PROCEDURE(fn_obj))
    {
        Procedure* proc = (Procedure*)fn_obj;
        VM_LOG("%s '%s' %p\n", call_type, proc->name->value, proc->chunk.code);

        if (g_bProfile) proc->call_count++;

        if (proc->arity != nargs)
        {
            yyerror(pvm->sk_state, "incorrect number of arguments to function");

            vm_pop_n(pvm, nargs);   // clear args from stack
            pvm->ip++;              // skip to the start of next instruction
            return;
        }

        vm_push_ip(pvm, pvm->ip);               // push return address onto stack
        pvm->ip = proc->chunk.code;             // set IP to function code
        vm_push(pvm, MAKE_NUM(pvm->fp));        // push old frame pointer
        vm_push(pvm, MAKE_NUM(proc->arity));    // push arity (for RET to use)
        pvm->fp = pvm->sp;                      // fp points past arity
        pvm->current_closure = NULL;            // not in a closure
    }
    else if (IS_NATIVE_FN(fn_obj))
    {
        NativeFunction* proc = (NativeFunction*)fn_obj;
        VM_LOG("CALL NATIVE FN '%s'\n", proc->name->value);
        SKFunc func = (SKFunc)proc->func;

        if (func)
        {
            int arity = proc->arity;
            if (arity >= 0)
            {
                if (nargs != arity)
                {
                    yyerror(pvm->sk_state, "incorrect number of arguments to native function");

                    // clear args from stack
                    vm_pop_n(pvm, nargs);
                    return;
                }
            }
            else
            {
                // Variable arity - push nargs for the native function to read
                vm_push(pvm, MAKE_NUM(nargs));
            }

            if (g_bProfile) {
                double t0 = profile_get_ms();
                func(pvm->sk_state);
                pvm->native_call_time_ms += profile_get_ms() - t0;
            } else {
                func(pvm->sk_state);
            }
        }
        else {
            fprintf(stderr, "NULL function pointer in NATIVE call!\n");
        }
    }
    else
        yyerror(pvm->sk_state, "procedure expected!");
}

//---------------------------------------------------------------
// Function pointer dispatch handlers
// Each returns VM_CONTINUE, VM_HALT, or VM_ERROR
//---------------------------------------------------------------
typedef VMExecResult (*OpHandler)(VM*);

static VMExecResult op_push(VM* pvm) { vm_push(pvm, (Object*)*pvm->ip++); return VM_CONTINUE; }
static VMExecResult op_pop(VM* pvm) { vm_pop(pvm); return VM_CONTINUE; }
static VMExecResult op_push_ip(VM* pvm) { vm_push_ip(pvm, pvm->ip); return VM_CONTINUE; }
static VMExecResult op_pop_ip(VM* pvm) { pvm->ip = vm_pop_ip(pvm); return VM_CONTINUE; }
static VMExecResult op_push_fp(VM* pvm) { vm_push(pvm, MAKE_NUM(pvm->fp)); return VM_CONTINUE; }
static VMExecResult op_pop_fp(VM* pvm) { pvm->fp = AS_NUM(vm_pop(pvm)); return VM_CONTINUE; }
static VMExecResult op_push_sp(VM* pvm) { vm_push(pvm, MAKE_NUM(pvm->sp)); return VM_CONTINUE; }
static VMExecResult op_pop_sp(VM* pvm) { pvm->sp = AS_NUM(vm_pop(pvm)); return VM_CONTINUE; }
static VMExecResult op_num(VM* pvm) { vm_push(pvm, MAKE_NUM(*pvm->ip++)); return VM_CONTINUE; }
static VMExecResult op_string(VM* pvm) { vm_push(pvm, vm_new_string(pvm, (const char*)*pvm->ip++)); return VM_CONTINUE; }
static VMExecResult op_true(VM* pvm) { vm_pushBoolean(pvm, TRUE); return VM_CONTINUE; }
static VMExecResult op_false(VM* pvm) { vm_pushBoolean(pvm, FALSE); return VM_CONTINUE; }
static VMExecResult op_dup(VM* pvm) { vm_push(pvm, vm_peek(pvm, 0)); return VM_CONTINUE; }
static VMExecResult op_nop(VM* pvm) { (void)pvm; return VM_CONTINUE; }
static VMExecResult op_halt(VM* pvm) { (void)pvm; return VM_HALT; }
static VMExecResult op_set_local(VM* pvm) { (void)pvm; return VM_CONTINUE; }

static VMExecResult op_store(VM* pvm) {
    Object *value = vm_pop(pvm), *key = vm_pop(pvm);
    if (!IS_STRING(key)) { yyerror(pvm->sk_state, "invalid variable name"); return VM_ERROR; }
    vm_update_global(pvm, key, value);
    return VM_CONTINUE;
}
static VMExecResult op_load(VM* pvm) {
    Object* key = vm_pop(pvm);
    vm_push(pvm, vm_get_global(pvm, key));
    return VM_CONTINUE;
}
static VMExecResult op_ret(VM* pvm) {
    Object* ret_val = vm_pop(pvm);
    int arity = AS_NUM(vm_peek(pvm, 0));
    int saved_fp = AS_NUM(vm_peek(pvm, 1));
    VMCode_t* ret_addr = AS_RETADDR(pvm->stack[pvm->fp - 3]);
    pvm->sp = pvm->fp - 3 - arity;
    pvm->fp = saved_fp;
    vm_push(pvm, ret_val);
    pvm->ip = ret_addr;
    return VM_CONTINUE;
}
static VMExecResult op_call(VM* pvm) {
    Object* fn_obj = (Object*)*pvm->ip++;
    int nargs = (int)*pvm->ip++;
    vm_call_procedure(pvm, fn_obj, nargs, "CALL");
    return VM_CONTINUE;
}
static VMExecResult op_icall(VM* pvm) {
    Object* fn_obj = vm_pop(pvm);
    int nargs = (int)*pvm->ip++;
    vm_call_procedure(pvm, fn_obj, nargs, "ICALL");
    return VM_CONTINUE;
}
static VMExecResult op_bra(VM* pvm) { pvm->ip += *pvm->ip; return VM_CONTINUE; }
static VMExecResult op_bnt(VM* pvm) {
    if (IS_FALSE(vm_pop(pvm))) pvm->ip += *pvm->ip; else pvm->ip++;
    return VM_CONTINUE;
}
static VMExecResult op_bnf(VM* pvm) {
    if (!IS_FALSE(vm_pop(pvm))) pvm->ip += *pvm->ip; else pvm->ip++;
    return VM_CONTINUE;
}
static VMExecResult op_get_local(VM* pvm) {
    vm_push(pvm, vm_fp_peek(pvm, (int)*pvm->ip++));
    return VM_CONTINUE;
}
static VMExecResult op_list(VM* pvm) {
    Pair* alist = NULL; Pair* pair = NULL;
    int n = (int)*pvm->ip++;
    for (int i = 0; i < n; i++) {
        vm_push(pvm, (Object*)pair);
        alist = (Pair*)vm_newObject(pvm, sizeof(Pair), OBJ_PAIR);
        vm_pop(pvm);
        alist->car = vm_pop(pvm);
        alist->cdr = (Object*)pair;
        pair = alist;
    }
    vm_push(pvm, (Object*)alist);
    return VM_CONTINUE;
}
static VMExecResult op_print_tos(VM* pvm) {
    if (pvm->sp) {
        fprintf(stdout, TERM_BRIGHT_BLUE);
        vm_print_object(vm_pop(pvm), VM_PRINT_FANCY, stdout);
        fprintf(stdout, TERM_RESET"\n");
    }
    return VM_CONTINUE;
}
static VMExecResult op_make_closure(VM* pvm) {
    int upvalue_count = (int)*pvm->ip++;
    Object* proc_obj = vm_pop(pvm);
    if (!IS_PROCEDURE(proc_obj)) { yyerror(pvm->sk_state, "OP_MAKE_CLOSURE: expected procedure"); return VM_ERROR; }
    vm_push(pvm, proc_obj);
    Closure* closure = (Closure*)vm_newObject(pvm, sizeof(Closure), OBJ_CLOSURE);
    closure->function = (Procedure*)vm_pop(pvm);
    closure->upvalue_count = upvalue_count;
    vm_push(pvm, (Object*)closure);
    if (upvalue_count > 0) {
        closure->upvalues = (Object**)vm_alloc(pvm, NULL, 0, sizeof(Object*) * upvalue_count);
        vm_pop(pvm);
        for (int i = upvalue_count - 1; i >= 0; i--) closure->upvalues[i] = vm_pop(pvm);
    } else { closure->upvalues = NULL; vm_pop(pvm); }
    vm_push(pvm, (Object*)closure);
    return VM_CONTINUE;
}
static VMExecResult op_get_upvalue(VM* pvm) {
    int idx = (int)*pvm->ip++;
    if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
        yyerror(pvm->sk_state, "OP_GET_UPVALUE: invalid upvalue index"); vm_push(pvm, NULL);
    } else { vm_push(pvm, pvm->current_closure->upvalues[idx]); }
    return VM_CONTINUE;
}
static VMExecResult op_set_upvalue(VM* pvm) {
    int idx = (int)*pvm->ip++;
    Object* value = vm_pop(pvm);
    if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
        yyerror(pvm->sk_state, "OP_SET_UPVALUE: invalid upvalue index");
    } else { pvm->current_closure->upvalues[idx] = value; }
    return VM_CONTINUE;
}
static VMExecResult op_tailcall(VM* pvm) {
    Object* fn_obj = (Object*)*pvm->ip++;
    int nargs = (int)*pvm->ip++;
    if (IS_PROCEDURE(fn_obj) || IS_CLOSURE(fn_obj)) {
        Procedure* proc = IS_CLOSURE(fn_obj) ? ((Closure*)fn_obj)->function : (Procedure*)fn_obj;
        if (proc->arity != nargs) { yyerror(pvm->sk_state, "TAILCALL: wrong number of arguments"); return VM_ERROR; }
        int old_arity = AS_NUM(pvm->stack[pvm->fp - 1]);
        if (nargs == old_arity) {
            int ob = pvm->fp - 3 - old_arity, nb = pvm->sp - nargs;
            for (int i = 0; i < nargs; i++) pvm->stack[ob + i] = pvm->stack[nb + i];
            pvm->sp = pvm->fp;
            pvm->current_closure = IS_CLOSURE(fn_obj) ? (Closure*)fn_obj : NULL;
            pvm->ip = proc->chunk.code;
        } else { vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL-FALLBACK"); }
    } else if (IS_NATIVE_FN(fn_obj)) { vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL-NATIVE");
    } else { yyerror(pvm->sk_state, "TAILCALL: not a procedure"); return VM_ERROR; }
    return VM_CONTINUE;
}
static VMExecResult op_tailcall_i(VM* pvm) {
    Object* fn_obj = vm_pop(pvm);
    int nargs = (int)*pvm->ip++;
    if (IS_PROCEDURE(fn_obj) || IS_CLOSURE(fn_obj)) {
        Procedure* proc = IS_CLOSURE(fn_obj) ? ((Closure*)fn_obj)->function : (Procedure*)fn_obj;
        if (proc->arity != nargs) { yyerror(pvm->sk_state, "TAILCALL_I: wrong number of arguments"); return VM_ERROR; }
        int old_arity = AS_NUM(pvm->stack[pvm->fp - 1]);
        if (nargs == old_arity) {
            int ob = pvm->fp - 3 - old_arity, nb = pvm->sp - nargs;
            for (int i = 0; i < nargs; i++) pvm->stack[ob + i] = pvm->stack[nb + i];
            pvm->sp = pvm->fp;
            pvm->current_closure = IS_CLOSURE(fn_obj) ? (Closure*)fn_obj : NULL;
            pvm->ip = proc->chunk.code;
        } else { vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL_I-FALLBACK"); }
    } else if (IS_NATIVE_FN(fn_obj)) { vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL_I-NATIVE");
    } else { yyerror(pvm->sk_state, "TAILCALL_I: not a procedure"); return VM_ERROR; }
    return VM_CONTINUE;
}
static VMExecResult op_unknown(VM* pvm) {
    fprintf(stderr, "Unknown opcode at ip=%p\n", pvm->ip - 1);
    return VM_ERROR;
}

// Function pointer dispatch table indexed by opcode
static const OpHandler op_dispatch[OP_LAST] = {
    op_halt, op_push, op_pop, op_push_ip, op_pop_ip,
    op_push_fp, op_pop_fp, op_push_sp, op_pop_sp,
    op_num, op_string, op_true, op_false,
    op_store, op_dup, op_load, op_ret, op_call, op_icall,
    op_tailcall, op_tailcall_i, op_bra, op_bnt, op_bnf, op_nop,
    op_make_closure, op_get_upvalue, op_set_upvalue, op_list,
    op_get_local, op_set_local, op_print_tos,
};

//---------------------------------------------------------------
// Execute a single VM instruction via function pointer table
// Used by vm_call_function's internal loop
//---------------------------------------------------------------
static VMExecResult vm_exec_one(VM* pvm)
{
    VMCode_t opcode = *pvm->ip++;

    if (g_bProfile && opcode < OP_LAST) {
        pvm->opcode_counts[opcode]++;
        pvm->total_opcodes++;
    }

    if (opcode >= 0 && opcode < OP_LAST)
        return op_dispatch[opcode](pvm);

    return op_unknown(pvm);
}

//---------------------------------------------------------------
// Call a function from C code with given arguments
// This allows native builtins like 'map' to call Scheme functions
//---------------------------------------------------------------
Object* vm_call_function(VM* pvm, Object* fn, int nargs, Object** args)
{
    if (!pvm || !fn) return NULL;

    if (IS_NATIVE_FN(fn))
    {
        // Native function - call directly
        NativeFunction* native = (NativeFunction*)fn;
        
        // Check arity for fixed-arity functions
        if (native->arity >= 0 && native->arity != nargs)
        {
            yyerror(pvm->sk_state, "vm_call_function: wrong number of arguments to native function");
            return NULL;
        }

        // Push arguments onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        // For variable arity, push arg count; for fixed arity, it's handled by the native
        if (native->arity < 0)
            vm_push(pvm, MAKE_NUM(nargs));

        // Call the native function
        native->func(pvm->sk_state);

        // Result is on stack
        Object* result = vm_pop(pvm);
        return result;
    }
    else if (IS_CLOSURE(fn))
    {
        // Closure - execute procedure with upvalue access
        Closure* closure = (Closure*)fn;
        Procedure* proc = closure->function;

        // Check arity
        if (proc->arity != nargs)
        {
            yyerror(pvm->sk_state, "vm_call_function: wrong number of arguments to closure");
            return NULL;
        }

        // Save current VM state
        VMCode_t* saved_ip = pvm->ip;
        int saved_fp = pvm->fp;
        int saved_sp = pvm->sp;
        Closure* saved_closure = pvm->current_closure;

        // Push arguments onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        // Set up frame: [args..., ret_addr, saved_fp, arity]
        vm_push_ip(pvm, NULL);               // NULL return address = exit sentinel
        vm_push(pvm, MAKE_NUM(pvm->fp));     // saved fp
        vm_push(pvm, MAKE_NUM(nargs));       // arity
        pvm->fp = pvm->sp;
        pvm->ip = proc->chunk.code;
        pvm->current_closure = closure;      // set for upvalue access

        // Execute bytecode until ip becomes NULL (from RET with NULL return addr)
        while (pvm->ip != NULL)
        {
            VMExecResult result = vm_exec_one(pvm);
            if (result == VM_HALT || result == VM_ERROR)
                break;
        }

        // Result is on stack
        Object* result = vm_pop(pvm);

        // Restore state
        pvm->ip = saved_ip;
        pvm->fp = saved_fp;
        pvm->sp = saved_sp;
        pvm->current_closure = saved_closure;

        return result;
    }
    else if (IS_PROCEDURE(fn))
    {
        // Scheme procedure - need to execute bytecode
        Procedure* proc = (Procedure*)fn;

        // Check arity
        if (proc->arity != nargs)
        {
            yyerror(pvm->sk_state, "vm_call_function: wrong number of arguments to procedure");
            return NULL;
        }

        // Save current VM state
        VMCode_t* saved_ip = pvm->ip;
        int saved_fp = pvm->fp;
        int saved_sp = pvm->sp;
        Closure* saved_closure = pvm->current_closure;

        // Push arguments onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        // Set up frame: [args..., ret_addr, saved_fp, arity]
        vm_push_ip(pvm, NULL);               // NULL return address = exit sentinel
        vm_push(pvm, MAKE_NUM(pvm->fp));     // saved fp
        vm_push(pvm, MAKE_NUM(nargs));       // arity
        pvm->fp = pvm->sp;
        pvm->ip = proc->chunk.code;
        pvm->current_closure = NULL;         // not in a closure

        // Execute bytecode until ip becomes NULL (from RET with NULL return addr)
        while (pvm->ip != NULL)
        {
            VMExecResult result = vm_exec_one(pvm);
            if (result == VM_HALT || result == VM_ERROR)
                break;
        }

        // Result is on stack
        Object* result = vm_pop(pvm);

        // Restore state
        pvm->ip = saved_ip;
        pvm->fp = saved_fp;
        pvm->sp = saved_sp;
        pvm->current_closure = saved_closure;

        return result;
    }

    yyerror(pvm->sk_state, "vm_call_function: not a function");
    return NULL;
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
// Uses computed gotos on GCC/Clang for faster dispatch,
// falls back to switch-based vm_exec_one on MSVC.
//---------------------------------------------------------------
void vm_exec(VM* pvm, VMCode_t *addr)
{
    if (pvm == NULL) {
        fprintf(stderr, "VM is NULL!\n");
        return;
    }
    
    if (!addr) {
        fprintf(stderr, "Invalid VM code address: %p\n", addr);
        return;
    }

    VM_LOG("\n---- VM Execution Trace ----\n");

    double start = g_bProfile ? profile_get_ms() : 0;

    pvm->ip = addr;

#if defined(__GNUC__) || defined(__clang__)
    // Threaded dispatch using computed gotos
    static void* dispatch_table[] = {
        &&L_HALT, &&L_PUSH, &&L_POP, &&L_PUSH_IP, &&L_POP_IP,
        &&L_PUSH_FP, &&L_POP_FP, &&L_PUSH_SP, &&L_POP_SP,
        &&L_NUM, &&L_STRING, &&L_TRUE, &&L_FALSE,
        &&L_STORE, &&L_DUP, &&L_LOAD, &&L_RET, &&L_CALL, &&L_ICALL,
        &&L_TAILCALL, &&L_TAILCALL_I, &&L_BRA, &&L_BNT, &&L_BNF, &&L_NOP,
        &&L_MAKE_CLOSURE, &&L_GET_UPVALUE, &&L_SET_UPVALUE, &&L_LIST,
        &&L_GET_LOCAL, &&L_SET_LOCAL, &&L_PRINT_TOS
    };

    #define DISPATCH() do { \
        VMCode_t op_ = *pvm->ip++; \
        if (g_bProfile && op_ < OP_LAST) { pvm->opcode_counts[op_]++; pvm->total_opcodes++; } \
        goto *dispatch_table[op_]; \
    } while(0)

    Object* obj;
    DISPATCH();

    L_PUSH:
        vm_push(pvm, (Object*)*pvm->ip++);
        DISPATCH();
    L_POP:
        vm_pop(pvm);
        DISPATCH();
    L_PUSH_IP:
        vm_push_ip(pvm, pvm->ip);
        DISPATCH();
    L_POP_IP:
        pvm->ip = vm_pop_ip(pvm);
        DISPATCH();
    L_PUSH_FP:
        vm_push(pvm, MAKE_NUM(pvm->fp));
        DISPATCH();
    L_POP_FP:
        { Object* fp_obj = vm_pop(pvm); pvm->fp = AS_NUM(fp_obj); }
        DISPATCH();
    L_PUSH_SP:
        vm_push(pvm, MAKE_NUM(pvm->sp));
        DISPATCH();
    L_POP_SP:
        { Object* sp_obj = vm_pop(pvm); pvm->sp = AS_NUM(sp_obj); }
        DISPATCH();
    L_DUP:
        obj = vm_peek(pvm, 0);
        vm_push(pvm, obj);
        DISPATCH();
    L_NUM:
        vm_push(pvm, MAKE_NUM(*pvm->ip++));
        DISPATCH();
    L_STRING:
        obj = vm_new_string(pvm, (const char*)*pvm->ip++);
        vm_push(pvm, obj);
        DISPATCH();
    L_TRUE:
        vm_pushBoolean(pvm, TRUE);
        DISPATCH();
    L_FALSE:
        vm_pushBoolean(pvm, FALSE);
        DISPATCH();
    L_RET:
        {
            Object* ret_val = vm_pop(pvm);
            int arity = AS_NUM(vm_peek(pvm, 0));
            int saved_fp = AS_NUM(vm_peek(pvm, 1));
            VMCode_t* ret_addr = AS_RETADDR(pvm->stack[pvm->fp - 3]);
            pvm->sp = pvm->fp - 3 - arity;
            pvm->fp = saved_fp;
            vm_push(pvm, ret_val);
            pvm->ip = ret_addr;
        }
        DISPATCH();
    L_HALT:
        goto done;
    L_CALL:
        {
            Object* fn_obj = (Object*)*pvm->ip++;
            int nargs = (int)*pvm->ip++;
            vm_call_procedure(pvm, fn_obj, nargs, "CALL");
        }
        DISPATCH();
    L_ICALL:
        {
            Object* fn_obj = vm_pop(pvm);
            int nargs = (int)*pvm->ip++;
            vm_call_procedure(pvm, fn_obj, nargs, "ICALL");
        }
        DISPATCH();
    L_BNT:
        obj = vm_pop(pvm);
        if (IS_FALSE(obj))
            pvm->ip += *pvm->ip;
        else
            pvm->ip++;
        DISPATCH();
    L_BNF:
        obj = vm_pop(pvm);
        if (!IS_FALSE(obj))
            pvm->ip += *pvm->ip;
        else
            pvm->ip++;
        DISPATCH();
    L_BRA:
        pvm->ip += *pvm->ip;
        DISPATCH();
    L_NOP:
        DISPATCH();
    L_STORE:
        {
            Object *value = vm_pop(pvm);
            Object *key = vm_pop(pvm);
            if (!IS_STRING(key)) {
                yyerror(pvm->sk_state, "invalid variable name");
                goto done;
            }
            vm_update_global(pvm, key, value);
        }
        DISPATCH();
    L_LOAD:
        {
            Object* key = vm_pop(pvm);
            Object* value = vm_get_global(pvm, key);
            vm_push(pvm, value);
        }
        DISPATCH();
    L_GET_LOCAL:
        {
            int index = (int)*pvm->ip++;
            Object* local_var = vm_fp_peek(pvm, index);
            vm_push(pvm, local_var);
        }
        DISPATCH();
    L_SET_LOCAL:
        DISPATCH();
    L_LIST:
        {
            Pair* alist = NULL;
            Pair* pair = NULL;
            int n = (int)*pvm->ip++;
            for (int i = 0; i < n; i++) {
                vm_push(pvm, (Object*)pair);
                alist = (Pair*)vm_newObject(pvm, sizeof(Pair), OBJ_PAIR);
                vm_pop(pvm);
                alist->car = vm_pop(pvm);
                alist->cdr = (Object*)pair;
                pair = alist;
            }
            vm_push(pvm, (Object*)alist);
        }
        DISPATCH();
    L_PRINT_TOS:
        if (pvm->sp) {
            fprintf(stdout, TERM_BRIGHT_BLUE);
            vm_print_object(vm_pop(pvm), VM_PRINT_FANCY, stdout);
            fprintf(stdout, TERM_RESET"\n");
        }
        DISPATCH();
    L_MAKE_CLOSURE:
        {
            int upvalue_count = (int)*pvm->ip++;
            Object* proc_obj = vm_pop(pvm);
            if (!IS_PROCEDURE(proc_obj)) {
                yyerror(pvm->sk_state, "OP_MAKE_CLOSURE: expected procedure");
                goto done;
            }
            vm_push(pvm, proc_obj);
            Closure* closure = (Closure*)vm_newObject(pvm, sizeof(Closure), OBJ_CLOSURE);
            closure->function = (Procedure*)vm_pop(pvm);
            closure->upvalue_count = upvalue_count;
            vm_push(pvm, (Object*)closure);
            if (upvalue_count > 0) {
                closure->upvalues = (Object**)vm_alloc(pvm, NULL, 0, sizeof(Object*) * upvalue_count);
                vm_pop(pvm);
                for (int i = upvalue_count - 1; i >= 0; i--)
                    closure->upvalues[i] = vm_pop(pvm);
            } else {
                closure->upvalues = NULL;
                vm_pop(pvm);
            }
            vm_push(pvm, (Object*)closure);
        }
        DISPATCH();
    L_GET_UPVALUE:
        {
            int idx = (int)*pvm->ip++;
            if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
                yyerror(pvm->sk_state, "OP_GET_UPVALUE: invalid upvalue index");
                vm_push(pvm, NULL);
            } else {
                vm_push(pvm, pvm->current_closure->upvalues[idx]);
            }
        }
        DISPATCH();
    L_SET_UPVALUE:
        {
            int idx = (int)*pvm->ip++;
            Object* value = vm_pop(pvm);
            if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
                yyerror(pvm->sk_state, "OP_SET_UPVALUE: invalid upvalue index");
            } else {
                pvm->current_closure->upvalues[idx] = value;
            }
        }
        DISPATCH();
    L_TAILCALL:
        {
            Object* fn_obj = (Object*)*pvm->ip++;
            int nargs = (int)*pvm->ip++;
            if (IS_PROCEDURE(fn_obj) || IS_CLOSURE(fn_obj)) {
                Procedure* proc = IS_CLOSURE(fn_obj) ? ((Closure*)fn_obj)->function : (Procedure*)fn_obj;
                if (proc->arity != nargs) { yyerror(pvm->sk_state, "TAILCALL: wrong number of arguments"); goto done; }
                int old_arity = AS_NUM(pvm->stack[pvm->fp - 1]);
                if (nargs == old_arity) {
                    int old_args_base = pvm->fp - 3 - old_arity;
                    int new_args_base = pvm->sp - nargs;
                    for (int i = 0; i < nargs; i++) pvm->stack[old_args_base + i] = pvm->stack[new_args_base + i];
                    pvm->sp = pvm->fp;
                    pvm->current_closure = IS_CLOSURE(fn_obj) ? (Closure*)fn_obj : NULL;
                    pvm->ip = proc->chunk.code;
                } else {
                    vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL-FALLBACK");
                }
            } else if (IS_NATIVE_FN(fn_obj)) {
                vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL-NATIVE");
            } else {
                yyerror(pvm->sk_state, "TAILCALL: not a procedure"); goto done;
            }
        }
        DISPATCH();
    L_TAILCALL_I:
        {
            Object* fn_obj = vm_pop(pvm);
            int nargs = (int)*pvm->ip++;
            if (IS_PROCEDURE(fn_obj) || IS_CLOSURE(fn_obj)) {
                Procedure* proc = IS_CLOSURE(fn_obj) ? ((Closure*)fn_obj)->function : (Procedure*)fn_obj;
                if (proc->arity != nargs) { yyerror(pvm->sk_state, "TAILCALL_I: wrong number of arguments"); goto done; }
                int old_arity = AS_NUM(pvm->stack[pvm->fp - 1]);
                if (nargs == old_arity) {
                    int old_args_base = pvm->fp - 3 - old_arity;
                    int new_args_base = pvm->sp - nargs;
                    for (int i = 0; i < nargs; i++) pvm->stack[old_args_base + i] = pvm->stack[new_args_base + i];
                    pvm->sp = pvm->fp;
                    pvm->current_closure = IS_CLOSURE(fn_obj) ? (Closure*)fn_obj : NULL;
                    pvm->ip = proc->chunk.code;
                } else {
                    vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL_I-FALLBACK");
                }
            } else if (IS_NATIVE_FN(fn_obj)) {
                vm_call_procedure(pvm, fn_obj, nargs, "TAILCALL_I-NATIVE");
            } else {
                yyerror(pvm->sk_state, "TAILCALL_I: not a procedure"); goto done;
            }
        }
        DISPATCH();

    #undef DISPATCH

done:
    if (g_bProfile)
        pvm->exec_time_ms += profile_get_ms() - start;

#else
    // MSVC: function pointer table dispatch
    while(1)
    {
        VMCode_t opcode = *pvm->ip++;
        if (g_bProfile && opcode < OP_LAST) {
            pvm->opcode_counts[opcode]++;
            pvm->total_opcodes++;
        }
        OpHandler handler = (opcode >= 0 && opcode < OP_LAST) ? op_dispatch[opcode] : op_unknown;
        VMExecResult result = handler(pvm);
        if (result == VM_HALT || result == VM_ERROR)
            break;
    }

    if (g_bProfile)
        pvm->exec_time_ms += profile_get_ms() - start;
#endif
}

//---------------------------------------------------------------
// Print profiling report
//---------------------------------------------------------------
static const char* opcode_names[] = {
    "HALT", "PUSH", "POP", "PUSH_IP", "POP_IP", "PUSH_FP", "POP_FP",
    "PUSH_SP", "POP_SP", "NUM", "STRING", "TRUE", "FALSE",
    "STORE", "DUP", "LOAD", "RET", "CALL", "ICALL",
    "TAILCALL", "TAILCALL_I", "BRA", "BNT", "BNF", "NOP",
    "MAKE_CLOSURE", "GET_UPVALUE", "SET_UPVALUE", "LIST",
    "GET_LOCAL", "SET_LOCAL", "PRINT_TOS"
};

void vm_print_profile(VM* pvm)
{
    if (!g_bProfile || !pvm) return;

    fprintf(stderr, "\n===== Profiling Report =====\n\n");

    // Timing summary
    double dispatch_time = pvm->exec_time_ms - pvm->gc_time_ms - pvm->native_call_time_ms;
    if (dispatch_time < 0) dispatch_time = 0;

    fprintf(stderr, "--- Timing ---\n");
    fprintf(stderr, "  Total execution:   %10.3f ms\n", pvm->exec_time_ms);
    fprintf(stderr, "  Dispatch (interp): %10.3f ms  (%5.1f%%)\n", dispatch_time,
            pvm->exec_time_ms > 0 ? dispatch_time * 100.0 / pvm->exec_time_ms : 0);
    fprintf(stderr, "  GC:                %10.3f ms  (%5.1f%%)\n", pvm->gc_time_ms,
            pvm->exec_time_ms > 0 ? pvm->gc_time_ms * 100.0 / pvm->exec_time_ms : 0);
    fprintf(stderr, "  Native calls:      %10.3f ms  (%5.1f%%)\n", pvm->native_call_time_ms,
            pvm->exec_time_ms > 0 ? pvm->native_call_time_ms * 100.0 / pvm->exec_time_ms : 0);
    fprintf(stderr, "  GC cycles:         %10zu\n\n", pvm->gc_cycles);

    // Opcode histogram (sorted by frequency)
    fprintf(stderr, "--- Opcode Frequency (total: %llu) ---\n", (unsigned long long)pvm->total_opcodes);

    // Sort by count (simple insertion sort on indices)
    int sorted[OP_LAST];
    for (int i = 0; i < OP_LAST; i++) sorted[i] = i;
    for (int i = 1; i < OP_LAST; i++) {
        int key = sorted[i];
        int j = i - 1;
        while (j >= 0 && pvm->opcode_counts[sorted[j]] < pvm->opcode_counts[key]) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    for (int i = 0; i < OP_LAST; i++) {
        int op = sorted[i];
        if (pvm->opcode_counts[op] == 0) break;
        const char* name = (op < (int)(sizeof(opcode_names)/sizeof(opcode_names[0]))) ? opcode_names[op] : "???";
        double pct = pvm->total_opcodes > 0 ? pvm->opcode_counts[op] * 100.0 / pvm->total_opcodes : 0;
        fprintf(stderr, "  %-14s %12llu  (%5.1f%%)\n", name, (unsigned long long)pvm->opcode_counts[op], pct);
    }

    // Hot functions - walk the GC object list to find Procedures
    fprintf(stderr, "\n--- Hot Functions (top 10) ---\n");
    typedef struct { const char* name; uint64_t count; } HotFunc;
    HotFunc hot[10];
    int hot_count = 0;

    for (Object* obj = pvm->head; obj != NULL; obj = obj->next) {
        if (obj->type == OBJ_PROCEDURE) {
            Procedure* proc = (Procedure*)obj;
            if (proc->call_count == 0) continue;
            const char* name = proc->name ? proc->name->value : "<anonymous>";

            // Insert into sorted top-10
            int pos = hot_count;
            while (pos > 0 && hot[pos - 1].count < proc->call_count) {
                if (pos < 10) hot[pos] = hot[pos - 1];
                pos--;
            }
            if (pos < 10) {
                hot[pos].name = name;
                hot[pos].count = proc->call_count;
                if (hot_count < 10) hot_count++;
            }
        }
    }

    for (int i = 0; i < hot_count; i++) {
        fprintf(stderr, "  %-20s %12llu calls\n", hot[i].name, (unsigned long long)hot[i].count);
    }

    fprintf(stderr, "\n============================\n");
}

//--------------------------------------------------------------
// Mark an object
//--------------------------------------------------------------
static void markObject(VM* pvm, Object* object) {
    if (!IS_OBJ(object)) return;

    if (IS_MARKED(object)) return;

	MARK_OBJ(object);

    if (pvm->grayCapacity < pvm->grayCount + 1) 
    {
        pvm->grayCapacity = GROW_CAPACITY(pvm->grayCapacity);
        pvm->grayStack = (Object**)realloc(pvm->grayStack, sizeof(Object*) * pvm->grayCapacity);

        // fatal error
        if (pvm->grayStack == NULL) exit(1);
    }

    pvm->grayStack[pvm->grayCount++] = object;
}

//--------------------------------------------------------------
// Mark all reachable objects
//--------------------------------------------------------------
static void markObjects(VM* pvm) {
    GC_LOG("-- gc marking\n");
    // Recursively mark all reachable objects

    size_t index = 0;
    ht_key_t key;
    ht_value_t value;

    // mark global objects
    while(ht_next(pvm->global_table, &index, &key, &value))
    {
        markObject(pvm, (Object*)key);
        markObject(pvm, (Object*)value);
    }

    // mark stack objects
    for (int i = 0; i < pvm->sp; i++) 
    {
        markObject(pvm, pvm->stack[i]);
    }

    // mark environment chain
    Environment* env = pvm->current_scope;
    while (env)
    {
        // mark environment object
        markObject(pvm, (Object*)env);

        // mark all bindings in the environment
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
        {
            markObject(pvm, (Object*)env_key);
            markObject(pvm, (Object*)env_value);
        }
        env = env->parent;
    }
}

//--------------------------------------------------------------
// Free an object
//--------------------------------------------------------------
static void freeObject(VM* pvm, Object* object)
{
	GC_LOG("-- gc freeing object of type %d\n", OBJ_TYPE(object));

    // we can only free Objects
    if (!IS_OBJ(object))
		return;

	// clean up based on object type
    switch (OBJ_TYPE(object))
    {
    case OBJ_PROCEDURE:
        {
            // free function-specific resources
            Procedure * proc = (Procedure*)object;
            GC_LOG("-- gc freeing proc: '%s'\n", ((String*)proc->name)->value);
            vm_alloc(pvm, proc->chunk.code, proc->chunk.capacity * sizeof(VMCode_t), 0);
            if (proc->upvalues) {
                vm_alloc(pvm, proc->upvalues, sizeof(UpvalueDesc) * proc->upvalue_count, 0);
                proc->upvalues = NULL;
            }
        }
		break;
    
    case OBJ_ENVIRONMENT:
        {
            // free environment-specific resources
            Environment* env = (Environment*)object;
            ht_free(env->bindings);
        }
        break;

    case OBJ_PORT:
    {
        Port* port = (Port*)object;
        GC_LOG("-- gc closing file: '%s'\n", ((String*)port->name)->value);

        if (port->fptr)
            fclose(port->fptr);

        port->fptr = NULL;
        port->name = NULL;
    }
        break;

    case OBJ_CLOSURE:
    {
        Closure* closure = (Closure*)object;
        if (closure->upvalues) {
            vm_alloc(pvm, closure->upvalues, sizeof(Object*) * closure->upvalue_count, 0);
            closure->upvalues = NULL;
        }
    }
        break;

    default:
        {
            // Dispatch to custom type descriptor if registered
            const SKTypeDesc* desc = vm_get_type_desc(pvm, OBJ_TYPE(object));
            if (desc && desc->free_fn)
                desc->free_fn(pvm, object);
        }
        break;
    }

    // track objects collected
    pvm->objectsCollected++;

    // Free the memory associated with the object
    vm_alloc(pvm, object, object->size, 0);
}

//--------------------------------------------------------------
// Sweep unmarked objects
//--------------------------------------------------------------
static int sweepObjects(VM* pvm) {
    GC_LOG("-- gc sweeping\n");

    int free_count = 0;

    Object** obj = &pvm->head;
    while (*obj) 
    {
        if (!IS_MARKED(*obj)) 
        {
            Object* unreached = *obj;
            *obj = unreached->next;

            freeObject(pvm, unreached);
            //vm_alloc(pvm, unreached, unreached->size, 0);
            pvm->numObjects--;
            free_count++;
        } else 
        {
            UNMARK_OBJ(*obj);   // Reset mark for next GC cycle
            obj = &(*obj)->next;
        }
    }

    GC_LOG("-- gc (%d) objects freed\n", free_count);

    return free_count;
}

//-------------------------------------------------------------
// Mark an object and add it to the gray stack
//-------------------------------------------------------------
static void blackenObject(VM* pvm, Object* object) 
{
    // Mark the object's references and add them to the gray stack
    // if they are not already marked
    //GC(_LOG("-- gc blackening object\n");

    if (!IS_OBJ(object)) return;

    switch(object->type) 
    {
    case OBJ_PAIR:
        {
            Pair* pair = (Pair*)object;
            markObject(pvm, pair->car);
            markObject(pvm, pair->cdr);
        }
        break;

    case OBJ_STRING:
        // Strings have no references to mark
        break;

    case OBJ_NATIVE_FN:
        {
            NativeFunction* nativeFn = (NativeFunction*)object;

            // If the native function has any references, mark them here

            // mark the function name object
            markObject(pvm, (Object*)nativeFn->name);
        }
        break;

    case OBJ_PROCEDURE:
        {
            Procedure* proc = (Procedure*)object;

            // mark any constants or references in the function's code chunk if needed

            // mark the function's name object
            markObject(pvm, (Object*)proc->name);
        }
        break;

    case OBJ_CLOSURE:
		{
            Closure* closure = (Closure*)object;
            markObject(pvm, (Object*)closure->function);
			
            // Mark all captured upvalues (check for NULL - upvalues array may not be allocated yet)
            if (closure->upvalues) {
                for (int i = 0; i < closure->upvalue_count; i++) {
                    markObject(pvm, closure->upvalues[i]);
                }
            }
		}
        break;

    case OBJ_ENVIRONMENT:
        {
        }
	    break;

    case OBJ_PORT:
        {
            markObject(pvm, (Object*)((Port*)object)->name);
        }
        break;

    default:
        {
            // Dispatch to custom type descriptor if registered
            const SKTypeDesc* desc = vm_get_type_desc(pvm, object->type);
            if (desc && desc->mark_fn)
                desc->mark_fn(pvm, object);
        }
        break;
    }
}

//--------------------------------------------------------------
// Trace references from the gray stack
//--------------------------------------------------------------
static void traceReferences(VM* pvm) {
    // Trace references from the gray stack and mark reachable objects
    GC_LOG("-- gc tracing references\n");

    while (pvm->grayCount > 0) 
    {
        Object* object = pvm->grayStack[--pvm->grayCount];
        blackenObject(pvm, object);
    }
}

//--------------------------------------------------------------
// Remove unmarked objects from the interned string table
//--------------------------------------------------------------
static void removeWhiteObjects(VM* pvm) {
    size_t index = 0;
    ht_key_t key;
    ht_value_t value;
    int SK_UNUSED removed = 0;

    // only remove unmarked strings
    while(ht_next(pvm->interned_strings, &index, &key, &value))
    {
        if (!IS_OBJ((Object*)value) || IS_MARKED((Object*)value))
            continue;

        if (HT_OK == ht_remove(pvm->interned_strings, key))
        {
            removed++;
            GC_LOG("-- gc removing interned string: '%s'\n", ((String*)value)->value);
        }
        else
            fprintf(stderr, "Error removing interned string during GC\n");
    }

    GC_LOG("-- gc removing (%d) white objects from intern table\n", removed);
}

//--------------------------------------------------------------
// Perform garbage collection
// Mark-and-sweep algorithm
//--------------------------------------------------------------
void vm_gc(VM* pvm) {
    GC_LOG("-- gc begin\n");

    double gc_start = g_bProfile ? profile_get_ms() : 0;

    size_t before SK_UNUSED = pvm->allocatedBytes;

    markObjects(pvm);

    // if there are any references from the stack or globals, we need to
    // follow those references and mark them as well
    traceReferences(pvm);

    // remove strings from the intern table that are not marked
    removeWhiteObjects(pvm);

    int freed SK_UNUSED = sweepObjects(pvm);

    pvm->gc_cycles++;

    if (g_bProfile)
        pvm->gc_time_ms += profile_get_ms() - gc_start;
    
    GC_LOG("-- gc end [%d / %zu] objs freed/live, [%zu / %zu] bytes freed/alloc'd, next at %zu.\n\n", freed, pvm->numObjects, before - pvm->allocatedBytes, pvm->allocatedBytes, pvm->nextGC);
}

//--------------------------------------------------------------
// Push a string onto the stack
//--------------------------------------------------------------
String *vm_push_string(VM* pvm, const char *str)
{
	Object* string_obj = vm_new_string(pvm, str);
    vm_push(pvm, string_obj);
	return (String*)string_obj;
}

//--------------------------------------------------------------
// Push a global variable onto the stack
//--------------------------------------------------------------
void vm_push_global(VM* pvm, const char *name)
{
    vm_push(pvm, vm_get_global(pvm, vm_new_string(pvm, name)));
}

//--------------------------------------------------------------
// Push a return address onto the return stack
//--------------------------------------------------------------
//--------------------------------------------------------------
// Push a return address onto the unified stack (as tagged value)
//--------------------------------------------------------------
void vm_push_ip(VM* pvm, VMCode_t *ip)
{
    // Push return address as tagged value onto unified stack
    vm_push(pvm, MAKE_RETADDR(ip));
}

//--------------------------------------------------------------
// Pop a return address from the unified stack
//--------------------------------------------------------------
VMCode_t* vm_pop_ip(VM* pvm) 
{
    Object* obj = vm_pop(pvm);
    if (!IS_RETADDR(obj)) {
        fprintf(stderr, "Expected return address on stack!\n");
        return NULL;
    }
    return AS_RETADDR(obj);
}

//--------------------------------------------------------------
// Push an object onto the stack
//--------------------------------------------------------------
void vm_push(VM* pvm, Object* obj) {
    if (pvm->sp >= VM_STACK_SIZE) {
        fprintf(stderr, "Stack overflow!\n");
        return;
    }

    pvm->stack[pvm->sp++] = obj;

    if (pvm->sp > pvm->max_stack_size) {
        pvm->max_stack_size = pvm->sp;
	}
}

//--------------------------------------------------------------
// Replace an object on the stack
//--------------------------------------------------------------
void vm_stack_replace_n(VM* pvm, int distance, Object* obj)
{
    int stack_loc = pvm->sp - 1 - distance;

    if (stack_loc < 0 || stack_loc >= VM_STACK_SIZE) {
        fprintf(stderr, "Stack bounds error!\n");
        return;
    }

    pvm->stack[stack_loc] = obj;
}

//--------------------------------------------------------------
// Pop an object from the stack
//--------------------------------------------------------------
Object* vm_pop(VM* pvm) {
    if (pvm->sp <= 0) {
        fprintf(stderr, "Stack underflow!\n");
        return NULL;
    }

    return pvm->stack[--pvm->sp];
}

//--------------------------------------------------------------
// Pop n objects from the stack
//--------------------------------------------------------------
void vm_pop_n(VM* pvm, int n)
{
    if (pvm->sp - n < 0) {
        fprintf(stderr, "Stack underflow on pop_n!\n");
        return;
    }
    pvm->sp -= n;
}

//--------------------------------------------------------------
// Peek at an object on the stack without popping it
//--------------------------------------------------------------
Object* vm_peek(VM* pvm, int distance) {
    if (pvm->sp - 1 - distance < 0) {
        fprintf(stderr, "Stack underflow on peek!\n");
        return NULL;
    }

    return pvm->stack[pvm->sp - 1 - distance];
}

//--------------------------------------------------------------
// Peek at an object relative to the frame without popping it
//--------------------------------------------------------------
Object* vm_fp_peek(VM* pvm, int distance) {
    if (pvm->fp - 1 - distance < 0) {
        fprintf(stderr, "Stack underflow on fp_peek!\n");
        return NULL;
    }

    return pvm->stack[pvm->fp - 1 - distance];
}

//--------------------------------------------------------------
// Custom realloc function for memory management
//--------------------------------------------------------------
void *vm_alloc(VM* pvm, void *ptr, size_t old_size, size_t new_size) {
    // nothing to do!
    if (new_size == old_size) {
        fprintf(stderr, "Realloc called with same size!\n");
        return ptr;
    }

    // possibly trigger GC
    if (new_size > old_size)
    {
        // Force a garbage collection on every allocation if stress GC is enabled
        // This is useful for testing purposes
        do
        {
            if (g_bGCStress)
                vm_gc(pvm);
        } while (0);

    }

    // free the memory
    if (new_size == 0) {
        free(ptr);

        // update allocatedBytes
        pvm->allocatedBytes -= old_size;

        return NULL;
    }

    // Allocate more or less memory
    void *new_ptr = realloc(ptr, new_size);

    if (!new_ptr) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }

    // Update allocated and peak bytes
    pvm->allocatedBytes += new_size - old_size;
    if (pvm->allocatedBytes > pvm->peakAllocatedBytes) {
        pvm->peakAllocatedBytes = pvm->allocatedBytes;
    }

    // update gc threshold
    if (pvm->allocatedBytes > pvm->nextGC) {
        vm_gc(pvm);
        pvm->nextGC = pvm->allocatedBytes * VM_GC_HEAP_GROWTH_FACTOR;
    }

    // return the new pointer
    return new_ptr;
}

//--------------------------------------------------------------
// Create a new object and add it to the VM's object list
//--------------------------------------------------------------
Object* vm_newObject(VM* pvm, size_t size, int type)
{
    if (size == 0)
        size = sizeof(Object);

    Object* obj = (Object*)vm_alloc(pvm, NULL, 0, size);
    memset(obj, 0, size); // zero out memory

	UNMARK_OBJ(obj);        // new objects are unmarked
    obj->type = type;       // default type
	obj->size = size;       // set size
    obj->next = pvm->head;  // Insert at the head of the object list
	pvm->head = obj;        // Update head to new object
	pvm->numObjects++;      // Increment object count

    // update peak objects count
    if (pvm->numObjects > pvm->peakObjects) {
        pvm->peakObjects = pvm->numObjects;
    }

    return obj;
}

//--------------------------------------------------------------
// Create a new environment object
//--------------------------------------------------------------
Object *vm_new_environment(VM* pvm)
{
    Environment* env = (Environment*)vm_newObject(pvm, sizeof(Environment), OBJ_ENVIRONMENT);
    env->bindings = ht_create();
    env->parent = pvm->current_scope;
    pvm->current_scope = env;

    return (Object*)env;
}

//--------------------------------------------------------------
// Unlink the top environment object from the environment chain
//--------------------------------------------------------------
void vm_free_environment(VM* pvm)
{
    if (pvm->current_scope)
        pvm->current_scope = pvm->current_scope->parent;

    // no need to explicitly free, GC will free this object once it has no refs
    // ht_free(&env->env);
    // vm_alloc(pvm, env, sizeof(Environment), 0);
}

//--------------------------------------------------------------
// Create a new boolean object
//--------------------------------------------------------------
Object* vm_newBoolean(VM* pvm, int val)
{
    if (val)
        return MAKE_TRUE();

    return MAKE_FALSE();
}

//--------------------------------------------------------------
// Push a boolean object onto the stack
//--------------------------------------------------------------
void vm_pushBoolean(VM* pvm, int val)
{
    Object* obj = vm_newBoolean(pvm, val);
    vm_push(pvm, obj);
}

//--------------------------------------------------------------
// Get an interned string object
//--------------------------------------------------------------
Object *vm_get_string(VM* pvm, const char* str)
{
    if (!pvm || !str) return NULL;

    // check interned strings table first
    ht_value_t val = ht_find(pvm->interned_strings, str);

    return (Object*)val;
}

//--------------------------------------------------------------
// Create a new string object
//--------------------------------------------------------------
Object *vm_new_string(VM* pvm, const char* str)
{
    if (!pvm || !str) return NULL;

    // check interned strings table first
	ht_value_t val = vm_get_string(pvm, str);
    
    // if string obj already exists, return it
    if (val)
        return (Object*)val;

    // otherwise create new string object
    Object* obj = vm_newObject(pvm, sizeof(String) + strlen(str) + 1, OBJ_STRING);

    String* stringObj = (String*)obj;
    strcpy((char*)stringObj->value, str);

    // and add to interned strings table
    ht_insert(pvm->interned_strings, stringObj->value, stringObj);

    return obj;
}

//--------------------------------------------------------------
// Initialize a code chunk
//--------------------------------------------------------------
void vm_init_chunk(CodeChunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    //chunk->lines = NULL;
}

//--------------------------------------------------------------
// Free a code chunk
//--------------------------------------------------------------
void vm_free_chunk(VM* pvm, CodeChunk* chunk)
{
	vm_alloc(pvm, chunk->code, chunk->capacity * sizeof(VMCode_t), 0);
	chunk->code = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
}

//--------------------------------------------------------------
// Create a new procedure object
//--------------------------------------------------------------
Object* vm_new_proc(VM* pvm)
{
    if (!pvm) return NULL;

    // create new procedure object
    Object* obj = vm_newObject(pvm, sizeof(Procedure), OBJ_PROCEDURE);

    Procedure* proc = AS_OBJ(Procedure, obj);
    if (!proc)
        return NULL;

    proc->arity = 0;
    proc->upvalues = NULL;
    proc->upvalue_count = 0;

	vm_init_chunk(&proc->chunk);

    return obj;
}

//--------------------------------------------------------------
// Create a new float object (tagged on 64-bit, heap on 32-bit)
//--------------------------------------------------------------
Object* vm_new_float(VM* pvm, float value)
{
#if SK_64BIT
    // 64-bit: return tagged float (no allocation needed)
    (void)pvm;  // unused on 64-bit
    return MAKE_TAGGED_FLOAT(value);
#else
    // 32-bit: heap allocate
    if (!pvm) return NULL;
    Object* obj = vm_newObject(pvm, sizeof(Float), OBJ_FLOAT);
    Float* flt = (Float*)obj;
    flt->value = value;
    return obj;
#endif
}

//--------------------------------------------------------------
// Get float value from object (handles both tagged and heap floats)
//--------------------------------------------------------------
float vm_get_float(Object* obj)
{
#if SK_64BIT
    if (IS_TAGGED_FLOAT(obj))
        return AS_TAGGED_FLOAT(obj);
#endif
    if (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_FLOAT)
        return ((Float*)obj)->value;
    return 0.0f;  // fallback
}

//--------------------------------------------------------------
// Define a global object
//--------------------------------------------------------------
int vm_define_global(VM *pvm, Object* key, Object *value)
{
    // determine if we can redefine a global
#ifdef VM_NO_REDEFINE
    return ht_insert(pvm->global_table, key, value);
#else
    return ht_add(pvm->global_table, key, value);
#endif

}

//--------------------------------------------------------------
// Update a global object
//--------------------------------------------------------------
int vm_update_global(VM *pvm, Object* key, Object *value)
{
    return ht_add(pvm->global_table, key, value);
}

//--------------------------------------------------------------
// Get a global object by key
//--------------------------------------------------------------
Object *vm_get_global(VM *pvm, Object* key)
{
    ht_value_t val = ht_find(pvm->global_table, key);
    return (Object*)val;
}

//--------------------------------------------------------------
// Set a variable in current scope
//--------------------------------------------------------------
void vm_set_var(VM* pvm, const char* name, Object* value)
{
    Object* key = vm_new_string(pvm, name);

    if (pvm->current_scope)
    {
        // set in current environment
        ht_add(pvm->current_scope->bindings, key, value);
        return;
	}
    else
        vm_update_global(pvm, key, value);
}

//--------------------------------------------------------------
// Get a variable from current scope
//--------------------------------------------------------------
Object* vm_get_local_var(VM* pvm, const char* name)
{
    Object* key = vm_new_string(pvm, name);

    if (pvm->current_scope)
    {
        // look in current environment chain
        Environment* env = pvm->current_scope;
        ht_value_t val;
        while (env)
        {
            val = ht_find(env->bindings, key);
            if (val)
                return (Object*)val;
            env = env->parent;
        }
	}

    return NULL;    // vm_get_global(pvm, key);
}

//--------------------------------------------------------------
// Extended local var lookup with scope depth
// Returns the binding value and sets scope_depth (0=current, 1=parent, etc.)
// Returns NULL and sets scope_depth to -1 if not found in any local scope
//--------------------------------------------------------------
Object* vm_get_local_var_ex(VM* pvm, const char* name, int* scope_depth)
{
    Object* key = vm_new_string(pvm, name);
    int depth = 0;

    if (pvm->current_scope)
    {
        Environment* env = pvm->current_scope;
        ht_value_t val;
        while (env)
        {
            val = ht_find(env->bindings, key);
            if (val)
            {
                if (scope_depth) *scope_depth = depth;
                return (Object*)val;
            }
            env = env->parent;
            depth++;
        }
	}

    if (scope_depth) *scope_depth = -1;  // Not found in local scopes
    return NULL;
}

//--------------------------------------------------------------
// Print the current VM stack for debugging
//--------------------------------------------------------------
void vm_print_stack(VM* pvm)
{
    printf("---- VM Stack (SP=%d) ----\n", pvm->sp);
    for (int i = pvm->sp - 1; i >= 0; i--)
    {
        Object* obj = pvm->stack[i];
        if (IS_NUM(obj))
        {
            printf("[%d] NUM: %d\n", i, AS_NUM(obj));
        }
        else if (IS_STRING(obj))
        {
            printf("[%d] STRING: '%s'\n", i, AS_STRING(obj)->value);
        }
        else
        {
            printf("[%d] OBJ TYPE: %d\n", i, OBJ_TYPE(obj));
        }
    }
    printf("-------------------------\n");
}

//--------------------------------------------------------------
// Create a new port object
//--------------------------------------------------------------
Port *vm_new_port(VM* pvm, const char* name, FILE* fptr)
{
    String *port_name= (String*)vm_new_string(pvm, name);
    vm_push(pvm, (Object*)port_name); // save on stack for GC

    Port* port = (Port*)vm_newObject(pvm, sizeof(Port), OBJ_PORT);

    port->name = port_name;
    port->fptr = fptr;

    vm_pop(pvm);    // remove string from stack
    return port;
}

//--------------------------------------------------------------
// Create default ports (stdin, stdout, stderr)
//--------------------------------------------------------------
int vm_create_default_ports(VM* pvm)
{
	Port* port;

    // create standard ports
	port = (Port*)vm_new_port(pvm, "stdin", stdin);
    vm_push(pvm, (Object*)port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-input-port"), (Object*)port);

    port = (Port*)vm_new_port(pvm, "stdout", stdout);
	vm_push(pvm, (Object*)port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-output-port"), (Object*)port);

	port = (Port*)vm_new_port(pvm, "stderr", stderr);
	vm_push(pvm, (Object*)port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-error-port"), (Object*)port);

    vm_pop_n(pvm, 3); // pop the ports off the stack

    return TRUE;
}

//--------------------------------------------------------------
// Free all objects in the VM
//--------------------------------------------------------------
void vm_freeObjects(VM* pvm) {
    Object* obj = pvm->head;

    while (obj) {
        Object* next = obj->next;
        free(obj);  // TODO - use vm_alloc for obj dtors?
        obj = next;
    }

    free(pvm->grayStack);
    pvm->grayStack = NULL;
    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->allocatedBytes = 0;
    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;
    pvm->gc_cycles = 0;
    pvm->max_call_depth = 0;
    pvm->max_stack_size = 0;
    pvm->sp = 0;
    pvm->fp = 0;
    pvm->objectsCollected = 0;
}

