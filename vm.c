//--------------------------------------------------------------
// vm.c - implementation of the virtual machine for SK
//
// Copyright (c) 2025 Mark Seminatore
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

static void vm_close_upvalues(VM* pvm, int from_slot);

// Where an aborted run continues (see vm_abort): the next instruction
// dispatched is OP_HALT. A branch taken from here adds its operand,
// OP_HALT == 0, so it stays put.
static VMCode_t vm_halt_code[] = { OP_HALT, OP_HALT };
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

static int op_operand_words(VMCode_t op);
static void free_chunk_strings(CodeChunk* chunk);
static void chunk_add_line(VM* pvm, CodeChunk* chunk, size_t offset);

//-----------------------------------------------
// Format a float as Scheme writes one: %g, with ".0" added when that
// looks like an integer, so 3.0 prints as 3.0, not 3 (which is exact)
//-----------------------------------------------
void vm_format_float(double value, char* buf, size_t size)
{
    snprintf(buf, size, "%g", value);
    if (!strpbrk(buf, ".eEnN") && strlen(buf) + 2 < size)     // not 2.5, 1e+10, inf or nan
        strcat(buf, ".0");
}

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

    if (IS_UNSPEC(obj))
    {
        fprintf(stream, "#<unspecified>");
        return;
    }

    if (IS_UNBOUND(obj))
    {
        fprintf(stream, "#<unbound>");
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
        char buf[32];
        vm_format_float(AS_TAGGED_FLOAT(obj), buf, sizeof(buf));
        fputs(buf, stream);
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

        case OBJ_CONTINUATION:
            fprintf(stream, "#<continuation>");
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
            char buf[32];
            vm_format_float(flt->value, buf, sizeof(buf));
            fputs(buf, stream);
        }
        break;

        case OBJ_SYMBOL:
            fprintf(stream, "%s", ((String*)obj)->value);
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

    pvm->stack = (Object**)malloc(sizeof(Object*) * VM_STACK_SIZE);
    CHECK_THAT_MSG(pvm->sk_state, pvm->stack != NULL, "vm_init: unable to allocate the stack");

    pvm->sp = 0;
    pvm->fp = 0;
    pvm->ip = 0;
    pvm->abort_exec = 0;
    pvm->exec_depth = 0;
    pvm->globals_version = 1;   // OP_LOAD_GLOBAL caches start at 0, i.e. empty
    pvm->open_upvalues = NULL;
    pvm->pending_call = NULL;
    pvm->pending_nargs = 0;
    pvm->call_root = NULL;
    pvm->escape_to = NULL;
    pvm->escape_value = NULL;
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

    // init interned symbols table (separate: a string and a symbol with the
    // same name are different objects)
    pvm->interned_symbols = ht_create();
    ht_set_hash_func(pvm->interned_symbols, HT_HASH_STRING);
    ht_set_compare_func(pvm->interned_symbols, compare_fn);

    // initialize current scope to NULL (ie. global scope)
    pvm->current_scope = NULL;
    pvm->current_closure = NULL;

    //pvm->vm_code_ptr = 0;
    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;

    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->grayStack = NULL;

    pvm->root_chunks = NULL;
    pvm->root_chunk_count = pvm->root_chunk_capacity = 0;
    pvm->compile_roots = NULL;
    pvm->compile_root_count = pvm->compile_root_capacity = 0;

    pvm->heap_limit = 0;                // none (sk_set_heap_limit)
    pvm->fatal_handler = NULL;          // print and exit (sk_set_fatal_handler)

    pvm->source_files = NULL;
    pvm->source_file_count = pvm->source_file_capacity = 0;

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

    free(pvm->stack);
    pvm->stack = NULL;
    pvm->sp = pvm->fp = 0;

    free(pvm->grayStack);
    pvm->grayStack = NULL;

    free(pvm->root_chunks);
    pvm->root_chunks = NULL;
    pvm->root_chunk_count = pvm->root_chunk_capacity = 0;
    free(pvm->compile_roots);
    pvm->compile_roots = NULL;
    pvm->compile_root_count = pvm->compile_root_capacity = 0;

    for (int i = 0; i < pvm->source_file_count; i++)
        free(pvm->source_files[i]);
    free(pvm->source_files);
    pvm->source_files = NULL;
    pvm->source_file_count = pvm->source_file_capacity = 0;

	ht_free(pvm->global_table);
	pvm->global_table = NULL;

    ht_free(pvm->interned_strings);
	pvm->interned_strings = NULL;

    ht_free(pvm->interned_symbols);
    pvm->interned_symbols = NULL;

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
    if (g_current_chunk == NULL)
        vm_fatal(pvm, "no current code chunk to emit into");

    CodeChunk* chunk = g_current_chunk;

    // grow chunk if necessary
	if (chunk->capacity < chunk->count + 1) {
		// The GC traces the code (markChunk) and may run inside vm_alloc, so
		// the chunk must stay valid throughout: allocate a new buffer, copy,
		// then free the old one (a realloc would leave chunk->code dangling)
		size_t old_capacity = chunk->capacity;
		size_t new_capacity = GROW_CAPACITY(old_capacity);
		VMCode_t* code = (VMCode_t*)vm_alloc(pvm, NULL, 0, new_capacity * sizeof(VMCode_t));
		if (chunk->count)
			memcpy(code, chunk->code, chunk->count * sizeof(VMCode_t));
		if (chunk->code)
			vm_alloc(pvm, chunk->code, old_capacity * sizeof(VMCode_t), 0);
		chunk->code = code;
		chunk->capacity = new_capacity;
	}

	chunk->code[chunk->count++] = opcode;
    chunk_add_line(pvm, chunk, chunk->count - 1);
    return (int)chunk->count-1;
}

//--------------------------------------------------------------
// The index of a source file name in VM.source_files, adding it if new
//--------------------------------------------------------------
static int source_file_id(VM* pvm, const char* name)
{
    for (int i = pvm->source_file_count - 1; i >= 0; i--)
        if (strcmp(pvm->source_files[i], name) == 0)
            return i;

    if (pvm->source_file_count == pvm->source_file_capacity)
    {
        pvm->source_file_capacity = GROW_CAPACITY(pvm->source_file_capacity);
        pvm->source_files = (char**)realloc(pvm->source_files, sizeof(char*) * pvm->source_file_capacity);
        vm_check_alloc(pvm, pvm->source_files);
    }
    pvm->source_files[pvm->source_file_count] = (char*)vm_check_alloc(pvm, sk_strdup(name));
    return pvm->source_file_count++;
}

//--------------------------------------------------------------
// Record the source location of the code word at 'offset', just emitted,
// where it differs from the previous word's (runtime error messages)
//--------------------------------------------------------------
static void chunk_add_line(VM* pvm, CodeChunk* chunk, size_t offset)
{
    const char* file;
    int line;
    if (!pvm->sk_state || !sk_source_location(pvm->sk_state, &file, &line))
        return;

    if (chunk->line_count > 0)
    {
        SourceLine* last = &chunk->lines[chunk->line_count - 1];
        if (last->line == line && strcmp(pvm->source_files[last->file], file) == 0)
            return;     // same place as the code before it
    }
    int file_id = source_file_id(pvm, file);

    if (chunk->line_count == chunk->line_capacity)
    {
        chunk->line_capacity = GROW_CAPACITY(chunk->line_capacity);
        chunk->lines = (SourceLine*)realloc(chunk->lines, sizeof(SourceLine) * chunk->line_capacity);
        vm_check_alloc(pvm, chunk->lines);
    }
    SourceLine* entry = &chunk->lines[chunk->line_count++];
    entry->offset = offset;
    entry->file = file_id;
    entry->line = line;
}

//--------------------------------------------------------------
// Drop a chunk's code from 'count' on (a discarded form, a cleared REPL line)
//--------------------------------------------------------------
void vm_chunk_truncate(CodeChunk* chunk, size_t count)
{
    chunk->count = count;
    while (chunk->line_count > 0 && chunk->lines[chunk->line_count - 1].offset >= count)
        chunk->line_count--;
}

//--------------------------------------------------------------
// The source location of the instruction running now, for a runtime
// error: in the current procedure's code, or in top-level code (a root
// chunk). Returns 0 if not known.
//--------------------------------------------------------------
int vm_current_location(VM* pvm, const char** file, int* line)
{
    VMCode_t* ip = pvm->ip;
    if (!ip || ip == vm_halt_code)
        return 0;

    // ip has moved past at least the opcode, so ip - 1 is in the instruction
    CodeChunk* chunk = NULL;
    if (pvm->fp > 0)
    {
        Object* fn = pvm->stack[pvm->fp - 1];
        Procedure* proc = IS_CLOSURE(fn) ? ((Closure*)fn)->function : (IS_PROCEDURE(fn) ? (Procedure*)fn : NULL);
        if (proc && ip > proc->chunk.code && ip <= proc->chunk.code + proc->chunk.count)
            chunk = &proc->chunk;
    }
    for (int i = pvm->root_chunk_count - 1; i >= 0 && !chunk; i--)
    {
        CodeChunk* root = pvm->root_chunks[i];
        if (ip > root->code && ip <= root->code + root->count)
            chunk = root;
    }
    if (!chunk || chunk->line_count == 0)
        return 0;

    size_t offset = (size_t)(ip - 1 - chunk->code);
    size_t lo = 0, hi = chunk->line_count;     // the last entry at or before offset
    while (hi - lo > 1)
    {
        size_t mid = (lo + hi) / 2;
        if (chunk->lines[mid].offset <= offset) lo = mid; else hi = mid;
    }
    *file = pvm->source_files[chunk->lines[lo].file];
    *line = chunk->lines[lo].line;
    return 1;
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
    vm_emit(pvm, (VMCode_t)vm_check_alloc(pvm, sk_strdup(str)));
}

void vm_emit_symbol(VM* pvm, const char* name)
{
    // strdup the name so the bytecode pointer remains valid
    // (yylval.word is a temp buffer that gets overwritten)
    vm_emit(pvm, OP_SYMBOL);
    vm_emit(pvm, (VMCode_t)vm_check_alloc(pvm, sk_strdup(name)));
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
    vm_chunk_truncate(g_current_chunk, 0);
}

//--------------------------------------------------------------
// Reset the VM
//--------------------------------------------------------------
int vm_reset(VM* pvm)
{
    CHECK_THAT_MSG(pvm->sk_state, pvm != NULL, "vm_reset: null VM pointer");

    vm_close_upvalues(pvm, 0);
    vm_clear_stack(pvm);
    vm_clear_code(pvm);
    pvm->fp = 0;
    pvm->current_closure = NULL;
    pvm->abort_exec = 0;

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

        case OP_SYMBOL:
            printf("SYMBOL '%s'\n", (const char*)*ip++);
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

        case OP_LIST_TAIL:
            printf("MAKE_LIST_TAIL count=%zd\n", *ip++);
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

        case OP_CAPTURE_LOCAL:
            printf("CAPTURE_LOCAL idx=%zd\n", *ip++);
            break;

        case OP_CAPTURE_UPVALUE:
            printf("CAPTURE_UPVALUE idx=%zd\n", *ip++);
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

        case OP_XCALL:
            printf("XCALL nargs=%zd\n", *ip++);
            break;

        case OP_TAILCALL_X:
            printf("TAILCALL_X nargs=%zd\n", *ip++);
            break;

        case OP_LOAD_GLOBAL:
        {
            Object* key = (Object*)*ip++;
            ip += 2;    // cached value and version
            printf("LOAD_GLOBAL '%s'\n", IS_STRING(key) ? ((String*)key)->value : "?");
        }
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

        case OP_DISCARD_TOS:
            printf("DISCARD_TOS\n");
            break;

        case OP_ADD: printf("ADD\n"); break;
        case OP_SUB: printf("SUB\n"); break;
        case OP_MUL: printf("MUL\n"); break;
        case OP_LT:  printf("LT\n"); break;
        case OP_LE:  printf("LE\n"); break;
        case OP_EQ_NUM: printf("EQ_NUM\n"); break;
        case OP_NUM_0: printf("NUM_0\n"); break;
        case OP_NUM_1: printf("NUM_1\n"); break;
        case OP_NUM_2: printf("NUM_2\n"); break;

        default:
            printf("Unknown opcode: %zd\n", instruction);
            break;
        }

        printf(TERM_RESET);
    }
}

// static void vm_call(VM* pvm, VMCode_t* addr)
// {
//     // push return address onto return stack
//     vm_push_ip(pvm, pvm->ip);

//     // set IP to function address
//     pvm->ip = addr;
// }

// static void vm_return(VM* pvm)
// {
//     // set IP to return address
//     pvm->ip = vm_pop_ip(pvm);
// }

// static void vm_push_frame(VM* pvm)
// {
//     // push current frame pointer onto stack
//     vm_push(pvm, MAKE_NUM(pvm->fp));

//     // set new frame pointer to current stack pointer
//     pvm->fp = pvm->sp;
// }

// static void vm_pop_frame(VM* pvm)
// {
//     // pop old frame pointer from stack
//     Object* fp_obj = vm_pop(pvm);
//     pvm->fp = AS_NUM(fp_obj);
// }

//---------------------------------------------------------------
// Frame layout: [args..., ret_addr, saved_fp, fn] with fp pointing past fn.
// The fn slot holds the Procedure or Closure being run. Its arity sizes the
// argument block, and its closure (if any) is the one to restore on return.
// Keeping it on the stack also keeps a running closure reachable for the GC.
//---------------------------------------------------------------
static void vm_exec_leave(VM* pvm);


//---------------------------------------------------------------
// The upvalue for stack slot 'slot', shared by every closure that captures
// it: reuse the open one if there is one, else create it.
//---------------------------------------------------------------
static Upvalue* vm_capture_slot(VM* pvm, int slot)
{
    Upvalue* prev = NULL;
    Upvalue* up = pvm->open_upvalues;
    while (up && up->slot > slot) {
        prev = up;
        up = up->next_open;
    }
    if (up && up->slot == slot)
        return up;

    // may GC: the open list is a root, and the new upvalue is not on it yet
    Upvalue* created = (Upvalue*)vm_newObject(pvm, sizeof(Upvalue), OBJ_UPVALUE);
    created->slot = slot;
    created->closed = NULL;

    // the GC may have run, but it does not unlink open upvalues, so prev/up still hold
    created->next_open = up;
    if (prev) prev->next_open = created; else pvm->open_upvalues = created;
    return created;
}

//---------------------------------------------------------------
// Close the open upvalues for stack slots >= from_slot, because the frame
// that owns those slots is going away (return, tail call or abort).
//---------------------------------------------------------------
static void vm_close_upvalues(VM* pvm, int from_slot)
{
    while (pvm->open_upvalues && pvm->open_upvalues->slot >= from_slot) {
        Upvalue* up = pvm->open_upvalues;
        up->closed = pvm->stack[up->slot];
        up->slot = -1;
        pvm->open_upvalues = up->next_open;
        up->next_open = NULL;
    }
}

static inline int frame_arity(Object* fn)
{
    return IS_CLOSURE(fn) ? ((Closure*)fn)->function->arity : ((Procedure*)fn)->arity;
}

static inline Closure* frame_closure(VM* pvm)
{
    if (pvm->fp <= 0)
        return NULL;    // top level: no frame

    Object* fn = pvm->stack[pvm->fp - 1];
    return IS_CLOSURE(fn) ? (Closure*)fn : NULL;
}

//---------------------------------------------------------------
// Every call yields exactly one value. A native that returns nothing
// (display, newline, ...) leaves the stack one short of result_sp, the
// stack depth expected once its arguments are consumed and its result
// pushed; give it the unspecified value so the caller's stack stays balanced.
//---------------------------------------------------------------
static void vm_native_result(VM* pvm, int result_sp)
{
    if (pvm->sp == result_sp - 1)
        vm_push(pvm, MAKE_UNSPEC());
}

//---------------------------------------------------------------
// Match the nargs arguments on top of the stack to proc's parameters. For
// a variadic procedure the arguments past the required ones become one
// list, the last parameter, so afterwards proc->arity values are on the
// stack. A wrong argument count is an error that aborts the run.
//---------------------------------------------------------------
static int vm_bind_args(VM* pvm, Object* fn_obj, Procedure* proc, int nargs)
{
    int required = proc->has_rest ? proc->arity - 1 : proc->arity;

    if (nargs < required || (!proc->has_rest && nargs > required))
    {
        char msg[160];
        snprintf(msg, sizeof(msg), "'%s' called with %d argument%s, expects %s%d",
            proc->name ? proc->name->value : "procedure", nargs, nargs == 1 ? "" : "s",
            proc->has_rest ? "at least " : "", required);
        vm_abort(pvm, msg);
        return FALSE;
    }

    if (proc->has_rest)
    {
        // building the rest list allocates, and the callee may no longer be
        // on the stack (ICALL popped it), so keep it alive meanwhile
        pvm->call_root = fn_obj;
        vm_build_list(pvm, nargs - required, NULL);     // the rest parameter
        pvm->call_root = NULL;
    }

    return TRUE;
}

//---------------------------------------------------------------
// Call the escape continuation k with value: unwind, like an abort but
// without an error, to the call/cc that made k, which then returns value.
// That call/cc must still be running; a continuation is not re-entrant.
//---------------------------------------------------------------
void vm_escape(VM* pvm, Object* k, Object* value)
{
    if (!((Continuation*)k)->valid)
    {
        vm_abort(pvm, "continuation called after its call/cc returned (only escaping continuations are supported)");
        return;
    }

    pvm->escape_to = k;
    pvm->escape_value = value;
    pvm->abort_exec = 1;            // every loop and call path stops as for an abort...
    pvm->ip = vm_halt_code;         // ...and the next dispatch halts (see vm_abort)
}

// a call to a continuation: one argument is the value (none gives unspecified)
static void vm_call_continuation(VM* pvm, Object* k, int nargs)
{
    if (nargs > 1)
    {
        vm_abort(pvm, "continuation called with more than one value");
        return;
    }
    Object* value = nargs == 1 ? vm_pop(pvm) : MAKE_UNSPEC();
    vm_escape(pvm, k, value);
}

//---------------------------------------------------------------
// A native asks the VM to call fn with the nargs values on top of the
// stack, and to make fn's result its own (apply does this). The native
// pushes the arguments, calls this and returns without a result.
//---------------------------------------------------------------
void vm_request_call(VM* pvm, Object* fn, int nargs)
{
    pvm->pending_call = fn;
    pvm->pending_nargs = nargs;
}

//---------------------------------------------------------------
// Run a native with its nargs arguments on the stack. Normally the result
// is then on the stack and this returns NULL. If the native requested a
// call (vm_request_call), this returns the procedure to call, with its
// *next_nargs arguments on the stack, for the caller to call normally or
// as a tail call.
//---------------------------------------------------------------
static Object* vm_run_native(VM* pvm, NativeFunction* native, int nargs, int* next_nargs)
{
    VM_LOG("CALL NATIVE FN '%s'\n", native->name->value);
    SKFunc func = (SKFunc)native->func;

    if (!func) {
        fprintf(stderr, "NULL function pointer in NATIVE call!\n");
        vm_pop_n(pvm, nargs);
        vm_push(pvm, MAKE_UNSPEC());
        return NULL;
    }

    // a wrong argument count aborts, as for a procedure (vm_bind_args)
    int arity = native->arity;
    int min = arity, max = arity;               // a fixed count
    switch (arity)
    {
    case SK_ARITY_ANY:          min = 0; max = -1; break;
    case SK_ARITY_ZERO_OR_ONE:  min = 0; max = 1;  break;
    case SK_ARITY_ONE_OR_MORE:  min = 1; max = -1; break;
    case SK_ARITY_TWO_OR_MORE:  min = 2; max = -1; break;
    }
    if (nargs < min || (max >= 0 && nargs > max))
    {
        char msg[160];
        snprintf(msg, sizeof(msg), "'%s' called with %d argument%s, expects %s%d",
            native->name ? native->name->value : "native function", nargs, nargs == 1 ? "" : "s",
            max < 0 ? "at least " : (min != max ? "at most " : ""), max < 0 ? min : max);
        vm_pop_n(pvm, nargs);
        vm_push(pvm, MAKE_UNSPEC());            // a result in their place
        vm_abort(pvm, msg);
        return NULL;
    }

    if (arity < 0)
    {
        // Variable arity - push nargs for the native function to read
        vm_push(pvm, MAKE_NUM(nargs));
    }

    int result_sp = pvm->sp - (arity >= 0 ? nargs : nargs + 1) + 1;

    if (g_bProfile) {
        double t0 = profile_get_ms();
        func(pvm->sk_state);
        pvm->native_call_time_ms += profile_get_ms() - t0;
    } else {
        func(pvm->sk_state);
    }

    if (pvm->pending_call)
    {
        Object* next = pvm->pending_call;
        *next_nargs = pvm->pending_nargs;
        pvm->pending_call = NULL;
        return next;
    }

    vm_native_result(pvm, result_sp);
    return NULL;
}

static void vm_call_procedure(VM* pvm, Object* fn_obj, int nargs, const char* call_type)
{
    if (IS_CLOSURE(fn_obj))
    {
        Closure* closure = (Closure*)fn_obj;
        Procedure* proc = closure->function;
        VM_LOG("%s CLOSURE '%s' %p\n", call_type, proc->name->value, proc->chunk.code);

        if (g_bProfile) proc->call_count++;

        if (!vm_bind_args(pvm, fn_obj, proc, nargs))
            return;

        vm_push_ip(pvm, pvm->ip);               // push return address
        pvm->ip = proc->chunk.code;             // set IP to function code
        vm_push(pvm, MAKE_NUM(pvm->fp));        // push old frame pointer
        vm_push(pvm, fn_obj);                   // push callee (arity and closure for RET)
        pvm->fp = pvm->sp;
        pvm->current_closure = closure;         // set current closure for upvalue access
    }
    else if (IS_PROCEDURE(fn_obj))
    {
        Procedure* proc = (Procedure*)fn_obj;
        VM_LOG("%s '%s' %p\n", call_type, proc->name->value, proc->chunk.code);

        if (g_bProfile) proc->call_count++;

        if (!vm_bind_args(pvm, fn_obj, proc, nargs))
            return;

        vm_push_ip(pvm, pvm->ip);               // push return address onto stack
        pvm->ip = proc->chunk.code;             // set IP to function code
        vm_push(pvm, MAKE_NUM(pvm->fp));        // push old frame pointer
        vm_push(pvm, fn_obj);                   // push callee (arity for RET to use)
        pvm->fp = pvm->sp;                      // fp points past callee slot
        pvm->current_closure = NULL;            // not in a closure
    }
    else if (IS_NATIVE_FN(fn_obj))
    {
        int next_nargs;
        Object* next = vm_run_native(pvm, (NativeFunction*)fn_obj, nargs, &next_nargs);
        if (next)
            vm_call_procedure(pvm, next, next_nargs, "NATIVE-REQUESTED CALL");
    }
    else if (IS_CONTINUATION(fn_obj))
        vm_call_continuation(pvm, fn_obj, nargs);
    else
        vm_abort(pvm, "not a procedure");
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
static VMExecResult op_pop_fp(VM* pvm) { Object* v = vm_pop(pvm); pvm->fp = AS_NUM(v); return VM_CONTINUE; }
static VMExecResult op_push_sp(VM* pvm) { vm_push(pvm, MAKE_NUM(pvm->sp)); return VM_CONTINUE; }
static VMExecResult op_pop_sp(VM* pvm) { Object* v = vm_pop(pvm); pvm->sp = AS_NUM(v); return VM_CONTINUE; }
static VMExecResult op_num(VM* pvm) { vm_push(pvm, MAKE_NUM(*pvm->ip++)); return VM_CONTINUE; }
static VMExecResult op_string(VM* pvm) { vm_push(pvm, vm_new_string(pvm, (const char*)*pvm->ip++)); return VM_CONTINUE; }
static VMExecResult op_symbol(VM* pvm) { vm_push(pvm, vm_new_symbol(pvm, (const char*)*pvm->ip++)); return VM_CONTINUE; }
static VMExecResult op_true(VM* pvm) { vm_pushBoolean(pvm, TRUE); return VM_CONTINUE; }
static VMExecResult op_false(VM* pvm) { vm_pushBoolean(pvm, FALSE); return VM_CONTINUE; }
static VMExecResult op_dup(VM* pvm) { vm_push(pvm, vm_peek(pvm, 0)); return VM_CONTINUE; }
static VMExecResult op_nop(VM* pvm) { (void)pvm; return VM_CONTINUE; }
static VMExecResult op_halt(VM* pvm) { (void)pvm; return VM_HALT; }
static VMExecResult op_set_local(VM* pvm) {
    int index = (int)*pvm->ip++;
    Object* value = vm_pop(pvm);
    vm_fp_set(pvm, index, value);
    return VM_CONTINUE;
}

static VMExecResult op_store(VM* pvm) {
    Object *value = vm_pop(pvm), *key = vm_pop(pvm);
    if (!IS_STRING(key)) { yyerror(pvm->sk_state, "invalid variable name"); return VM_ERROR; }
    vm_update_global(pvm, key, value);
    return VM_CONTINUE;
}
// push the value of the global 'key'; reading an unbound global is an error
static VMExecResult vm_load_global(VM* pvm, Object* key) {
    Object* value = vm_get_global(pvm, key);
    if (IS_UNBOUND(value) || (value == NULL && !vm_has_global(pvm, key))) {
        char msg[128];
        snprintf(msg, sizeof(msg), "unbound variable '%s'", IS_STRING(key) ? ((String*)key)->value : "?");
        vm_abort(pvm, msg);
        return VM_ERROR;
    }
    vm_push(pvm, value);
    return VM_CONTINUE;
}
static VMExecResult op_load(VM* pvm) {
    return vm_load_global(pvm, vm_pop(pvm));
}
// OP_LOAD_GLOBAL key cached_value cache_version
// The instruction caches the value it last read. Any global write bumps
// globals_version, so a cache is valid exactly while its version matches.
static VMExecResult op_load_global(VM* pvm) {
    VMCode_t* operands = pvm->ip;
    pvm->ip += 3;

    if ((size_t)operands[2] == pvm->globals_version) {
        vm_push(pvm, (Object*)operands[1]);
        return VM_CONTINUE;
    }

    VMExecResult result = vm_load_global(pvm, (Object*)operands[0]);
    if (result == VM_CONTINUE) {
        operands[1] = (VMCode_t)vm_peek(pvm, 0);
        operands[2] = (VMCode_t)pvm->globals_version;
    }
    return result;
}
static VMExecResult op_ret(VM* pvm) {
    Object* ret_val = vm_pop(pvm);
    // Read the frame header relative to fp, not the top of the stack. The values
    // of non-final body expressions (and natives such as display, which push
    // nothing) can leave a different number of temporaries above the frame, so
    // the top of the stack is not reliably the callee slot.
    int arity = frame_arity(pvm->stack[pvm->fp - 1]);
    int saved_fp = AS_NUM(pvm->stack[pvm->fp - 2]);
    VMCode_t* ret_addr = AS_RETADDR(pvm->stack[pvm->fp - 3]);
    pvm->sp = pvm->fp - 3 - arity;
    vm_close_upvalues(pvm, pvm->sp);            // the frame's slots are going away
    pvm->fp = saved_fp;
    pvm->current_closure = frame_closure(pvm);  // back to the caller's closure
    vm_push(pvm, ret_val);
    pvm->ip = ret_addr;
    return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;    // ip was just reset; see vm_abort
}
// Calls set ip (or run a native that may abort), so they test for an abort
// themselves (see vm_abort).
static VMExecResult op_call(VM* pvm) {
    Object* fn_obj = (Object*)*pvm->ip++;
    int nargs = (int)*pvm->ip++;
    vm_call_procedure(pvm, fn_obj, nargs, "CALL");
    return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;
}
static VMExecResult op_icall(VM* pvm) {
    Object* fn_obj = vm_pop(pvm);
    if (pvm->abort_exec) return VM_ERROR;
    int nargs = (int)*pvm->ip++;
    vm_call_procedure(pvm, fn_obj, nargs, "ICALL");
    return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;
}
static VMExecResult op_xcall(VM* pvm) {
    // Function is under the args: stack [fn, arg1, ..., argN]
    int nargs = (int)*pvm->ip++;
    assert(pvm->sp > nargs && "Stack underflow in op_xcall");
    Object* fn_obj = vm_peek(pvm, nargs);   // peek past the N args
    // Shift args down by one to overwrite the fn slot
    for (int i = nargs; i > 0; i--)
        pvm->stack[pvm->sp - 1 - i] = pvm->stack[pvm->sp - i];
    pvm->sp--;  // we consumed one extra slot (the fn)
    vm_call_procedure(pvm, fn_obj, nargs, "XCALL");
    return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;
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
// cons the top n stack values, in push order, onto 'tail'
void vm_build_list(VM* pvm, int n, Object* tail) {
    Object* pair = tail;
    for (int i = 0; i < n; i++) {
        vm_push(pvm, pair);             // keep the list built so far alive
        Pair* alist = (Pair*)vm_newObject(pvm, sizeof(Pair), OBJ_PAIR);
        vm_pop(pvm);
        alist->car = vm_pop(pvm);
        alist->cdr = pair;
        pair = (Object*)alist;
    }
    vm_push(pvm, pair);
}
static VMExecResult op_list(VM* pvm) {
    int n = (int)*pvm->ip++;
    vm_build_list(pvm, n, NULL);
    return VM_CONTINUE;
}
// OP_LIST_TAIL n: stack [e1, ..., en, tail] -> (e1 ... en . tail)
static VMExecResult op_list_tail(VM* pvm) {
    int n = (int)*pvm->ip++;
    Object* tail = vm_pop(pvm);
    vm_build_list(pvm, n, tail);
    return VM_CONTINUE;
}
static VMExecResult op_discard_tos(VM* pvm) {
    if (pvm->sp)
        vm_pop(pvm);    // like op_print_tos: some top-level forms (define) leave nothing
    return VM_CONTINUE;
}
static VMExecResult op_print_tos(VM* pvm) {
    if (pvm->sp && IS_UNSPEC(vm_peek(pvm, 0))) {
        vm_pop(pvm);    // nothing worth echoing
    } else if (pvm->sp) {
        fprintf(stdout, TERM_BRIGHT_BLUE);
        vm_print_object(vm_pop(pvm), VM_PRINT_FANCY, stdout);
        fprintf(stdout, TERM_RESET"\n");
    }
    return VM_CONTINUE;
}
// OP_MAKE_CLOSURE n: stack [upvalue0, ..., upvalue(n-1), proc] -> [closure].
// The upvalues come from OP_CAPTURE_LOCAL / OP_CAPTURE_UPVALUE.
static VMExecResult op_make_closure(VM* pvm) {
    int upvalue_count = (int)*pvm->ip++;
    Object* proc_obj = vm_pop(pvm);
    if (!IS_PROCEDURE(proc_obj)) { yyerror(pvm->sk_state, "OP_MAKE_CLOSURE: expected procedure"); return VM_ERROR; }
    vm_push(pvm, proc_obj);
    Closure* closure = (Closure*)vm_newObject(pvm, sizeof(Closure), OBJ_CLOSURE);
    closure->function = (Procedure*)vm_pop(pvm);
    closure->upvalues = NULL;           // nothing for the GC to trace yet
    closure->upvalue_count = 0;
    vm_push(pvm, (Object*)closure);     // keep it alive while the array is allocated
    if (upvalue_count > 0) {
        Upvalue** upvalues = (Upvalue**)vm_alloc(pvm, NULL, 0, sizeof(Upvalue*) * upvalue_count);
        vm_pop(pvm);
        for (int i = upvalue_count - 1; i >= 0; i--) upvalues[i] = (Upvalue*)vm_pop(pvm);
        closure->upvalues = upvalues;
        closure->upvalue_count = upvalue_count;
    } else { vm_pop(pvm); }
    vm_push(pvm, (Object*)closure);
    return VM_CONTINUE;
}
static VMExecResult op_capture_local(VM* pvm) {
    int slot = pvm->fp - 1 - (int)*pvm->ip++;   // same addressing as OP_GET_LOCAL
    vm_push(pvm, (Object*)vm_capture_slot(pvm, slot));
    return VM_CONTINUE;
}
static VMExecResult op_capture_upvalue(VM* pvm) {
    int idx = (int)*pvm->ip++;
    if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
        yyerror(pvm->sk_state, "OP_CAPTURE_UPVALUE: invalid upvalue index"); vm_push(pvm, NULL);
    } else { vm_push(pvm, (Object*)pvm->current_closure->upvalues[idx]); }  // shared, not copied
    return VM_CONTINUE;
}
static VMExecResult op_get_upvalue(VM* pvm) {
    int idx = (int)*pvm->ip++;
    if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
        yyerror(pvm->sk_state, "OP_GET_UPVALUE: invalid upvalue index"); vm_push(pvm, NULL);
    } else {
        Upvalue* up = pvm->current_closure->upvalues[idx];
        vm_push(pvm, up->slot >= 0 ? pvm->stack[up->slot] : up->closed);
    }
    return VM_CONTINUE;
}
static VMExecResult op_set_upvalue(VM* pvm) {
    int idx = (int)*pvm->ip++;
    Object* value = vm_pop(pvm);
    if (!pvm->current_closure || idx < 0 || idx >= pvm->current_closure->upvalue_count) {
        yyerror(pvm->sk_state, "OP_SET_UPVALUE: invalid upvalue index");
    } else {
        Upvalue* up = pvm->current_closure->upvalues[idx];
        if (up->slot >= 0) pvm->stack[up->slot] = value; else up->closed = value;
    }
    return VM_CONTINUE;
}
//---------------------------------------------------------------
// Tail call: replace the current frame with one for fn_obj instead of
// pushing a new frame, so tail-recursive loops run in constant stack.
//
//   before: [old args (old_arity)][ret][saved_fp][fn]  ...temps  [new args (nargs)]
//                                                    ^fp                          ^sp
//   after:  [new args (nargs)][ret][saved_fp][fn_obj]
//                                                    ^fp = sp
//
// The arguments move down to where the old ones started, and the
// caller's return address and frame pointer are kept, so the callee
// returns straight to our caller. This works for any arity.
//---------------------------------------------------------------
static VMExecResult vm_tail_call(VM* pvm, Object* fn_obj, int nargs, const char* call_type)
{
    if (IS_PROCEDURE(fn_obj) || IS_CLOSURE(fn_obj)) {
        Procedure* proc = IS_CLOSURE(fn_obj) ? ((Closure*)fn_obj)->function : (Procedure*)fn_obj;

        if (pvm->fp < 3) {
            // no frame to replace (not inside a function): an ordinary call
            vm_call_procedure(pvm, fn_obj, nargs, call_type);
            return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;
        }

        if (!vm_bind_args(pvm, fn_obj, proc, nargs))
            return VM_ERROR;
        nargs = proc->arity;        // a rest list is now one argument

        int old_arity = frame_arity(pvm->stack[pvm->fp - 1]);
        Object* ret_addr = pvm->stack[pvm->fp - 3];     // read the header first: with more
        Object* saved_fp = pvm->stack[pvm->fp - 2];     // args than before, the move overwrites it

        int base = pvm->fp - 3 - old_arity, src = pvm->sp - nargs;
        vm_close_upvalues(pvm, base);                   // the old frame's slots are reused
        for (int i = 0; i < nargs; i++)                 // base <= src, so copying upward is safe
            pvm->stack[base + i] = pvm->stack[src + i];

        pvm->sp = base + nargs;
        pvm->stack[pvm->sp++] = ret_addr;
        pvm->stack[pvm->sp++] = saved_fp;
        pvm->stack[pvm->sp++] = fn_obj;
        pvm->fp = pvm->sp;

        if (g_bProfile) proc->call_count++;
        pvm->current_closure = IS_CLOSURE(fn_obj) ? (Closure*)fn_obj : NULL;
        pvm->ip = proc->chunk.code;
    } else if (IS_NATIVE_FN(fn_obj)) {
        // a call the native requests (apply) is in tail position too
        int next_nargs;
        Object* next = vm_run_native(pvm, (NativeFunction*)fn_obj, nargs, &next_nargs);
        if (next && !pvm->abort_exec)
            return vm_tail_call(pvm, next, next_nargs, call_type);
    } else if (IS_CONTINUATION(fn_obj)) {
        vm_call_continuation(pvm, fn_obj, nargs);
    } else {
        vm_abort(pvm, "not a procedure");
        return VM_ERROR;
    }
    return pvm->abort_exec ? VM_ERROR : VM_CONTINUE;  // see vm_abort
}
static VMExecResult op_tailcall(VM* pvm) {
    Object* fn_obj = (Object*)*pvm->ip++;
    int nargs = (int)*pvm->ip++;
    return vm_tail_call(pvm, fn_obj, nargs, "TAILCALL");
}
static VMExecResult op_tailcall_i(VM* pvm) {
    Object* fn_obj = vm_pop(pvm);
    if (pvm->abort_exec) return VM_ERROR;
    int nargs = (int)*pvm->ip++;
    return vm_tail_call(pvm, fn_obj, nargs, "TAILCALL_I");
}
static VMExecResult op_tailcall_x(VM* pvm) {
    // Function is under the args: stack [fn, arg1, ..., argN], as for OP_XCALL
    int nargs = (int)*pvm->ip++;
    Object* fn_obj = vm_peek(pvm, nargs);
    for (int i = nargs; i > 0; i--)
        pvm->stack[pvm->sp - 1 - i] = pvm->stack[pvm->sp - i];
    pvm->sp--;
    return vm_tail_call(pvm, fn_obj, nargs, "TAILCALL_X");
}
static VMExecResult op_unknown(VM* pvm) {
    fprintf(stderr, "Unknown opcode at ip=%p\n", pvm->ip - 1);
    return VM_ERROR;
}

// Specialized arithmetic opcodes
// These bypass vm_push/vm_pop for speed; debug assertions guard correctness
// The slow path of an inline arithmetic op (ints are handled first):
// both operands must be numbers. Aborts and returns 0 if not.
static int vm_check_numeric(VM* pvm, Object* a, Object* b, const char* op) {
    if (IS_NUMERIC(a) && IS_NUMERIC(b))
        return 1;
    char msg[64];
    snprintf(msg, sizeof(msg), "arguments to '%s' must be numbers", op);
    vm_abort(pvm, msg);
    return 0;
}
static VMExecResult op_add(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_add");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        pvm->stack[pvm->sp++] = MAKE_NUM(AS_NUM(a) + AS_NUM(b));
    else if (!vm_check_numeric(pvm, a, b, "+"))
        return VM_ERROR;
    else
        pvm->stack[pvm->sp++] = vm_new_float(pvm, (float)(AS_DOUBLE(a) + AS_DOUBLE(b)));
    return VM_CONTINUE;
}
static VMExecResult op_sub(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_sub");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        pvm->stack[pvm->sp++] = MAKE_NUM(AS_NUM(a) - AS_NUM(b));
    else if (!vm_check_numeric(pvm, a, b, "-"))
        return VM_ERROR;
    else
        pvm->stack[pvm->sp++] = vm_new_float(pvm, (float)(AS_DOUBLE(a) - AS_DOUBLE(b)));
    return VM_CONTINUE;
}
static VMExecResult op_mul(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_mul");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        pvm->stack[pvm->sp++] = MAKE_NUM(AS_NUM(a) * AS_NUM(b));
    else if (!vm_check_numeric(pvm, a, b, "*"))
        return VM_ERROR;
    else
        pvm->stack[pvm->sp++] = vm_new_float(pvm, (float)(AS_DOUBLE(a) * AS_DOUBLE(b)));
    return VM_CONTINUE;
}
static VMExecResult op_lt(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_lt");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        vm_push(pvm, AS_NUM(a) < AS_NUM(b) ? MAKE_TRUE() : MAKE_FALSE());
    else if (!vm_check_numeric(pvm, a, b, "<"))
        return VM_ERROR;
    else
        vm_push(pvm, AS_DOUBLE(a) < AS_DOUBLE(b) ? MAKE_TRUE() : MAKE_FALSE());
    return VM_CONTINUE;
}
static VMExecResult op_le(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_le");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        vm_push(pvm, AS_NUM(a) <= AS_NUM(b) ? MAKE_TRUE() : MAKE_FALSE());
    else if (!vm_check_numeric(pvm, a, b, "<="))
        return VM_ERROR;
    else
        vm_push(pvm, AS_DOUBLE(a) <= AS_DOUBLE(b) ? MAKE_TRUE() : MAKE_FALSE());
    return VM_CONTINUE;
}
static VMExecResult op_eq_num(VM* pvm) {
    assert(pvm->sp >= 2 && "Stack underflow in op_eq_num");
    Object* b = pvm->stack[--pvm->sp];
    Object* a = pvm->stack[--pvm->sp];
    if (IS_NUM(a) && IS_NUM(b))
        vm_push(pvm, AS_NUM(a) == AS_NUM(b) ? MAKE_TRUE() : MAKE_FALSE());
    else if (!vm_check_numeric(pvm, a, b, "="))
        return VM_ERROR;
    else
        vm_push(pvm, AS_DOUBLE(a) == AS_DOUBLE(b) ? MAKE_TRUE() : MAKE_FALSE());
    return VM_CONTINUE;
}

// Small-constant opcodes
static VMExecResult op_num_0(VM* pvm) { vm_push(pvm, MAKE_NUM(0)); return VM_CONTINUE; }
static VMExecResult op_num_1(VM* pvm) { vm_push(pvm, MAKE_NUM(1)); return VM_CONTINUE; }
static VMExecResult op_num_2(VM* pvm) { vm_push(pvm, MAKE_NUM(2)); return VM_CONTINUE; }
// Function pointer dispatch table indexed by opcode
static const OpHandler op_dispatch[OP_LAST] = {
    op_halt, op_push, op_pop, op_push_ip, op_pop_ip,
    op_push_fp, op_pop_fp, op_push_sp, op_pop_sp,
    op_num, op_string, op_true, op_false,
    op_store, op_dup, op_load, op_ret, op_call, op_icall, op_xcall,
    op_tailcall, op_tailcall_i, op_bra, op_bnt, op_bnf, op_nop,
    op_make_closure, op_get_upvalue, op_set_upvalue, op_list,
    op_get_local, op_set_local, op_print_tos,
    op_add, op_sub, op_mul, op_lt, op_le, op_eq_num,
    op_num_0, op_num_1, op_num_2,
    op_symbol, op_tailcall_x, op_load_global, op_discard_tos,
    op_capture_local, op_capture_upvalue, op_list_tail,
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
    if (!pvm || !fn || pvm->abort_exec) return NULL;

    if (pvm->exec_depth >= VM_MAX_NATIVE_DEPTH)
    {
        // each level nests C calls; stop before the C stack runs out
        vm_abort(pvm, "native calls nested too deeply (recursion through map, apply or call/cc)");
        return NULL;
    }

    if (IS_CONTINUATION(fn))
    {
        vm_escape(pvm, fn, nargs > 0 ? args[0] : MAKE_UNSPEC());
        return NULL;
    }

    if (IS_NATIVE_FN(fn))
    {
        // Native function - call directly
        NativeFunction* native = (NativeFunction*)fn;

        // Push arguments (vm_run_native checks their number) onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        int next_nargs;
        Object* next = vm_run_native(pvm, native, nargs, &next_nargs);
        if (next && !pvm->abort_exec)
        {
            // the native requested a call (apply): make it from here, with
            // its arguments taken off the stack. Nothing allocates between
            // the pops and vm_call_function pushing them back.
            Object** next_args = (Object**)vm_check_alloc(pvm, malloc(sizeof(Object*) * (next_nargs ? next_nargs : 1)));
            pvm->call_root = next;
            for (int i = next_nargs - 1; i >= 0; i--)
                next_args[i] = vm_pop(pvm);
            Object* result = vm_call_function(pvm, next, next_nargs, next_args);
            pvm->call_root = NULL;
            free(next_args);
            return result;
        }

        // Result is on stack
        Object* result = pvm->abort_exec ? NULL : vm_pop(pvm);
        return result;
    }
    else if (IS_CLOSURE(fn))
    {
        // Closure - execute procedure with upvalue access
        Closure* closure = (Closure*)fn;
        Procedure* proc = closure->function;

        // Save current VM state
        VMCode_t* saved_ip = pvm->ip;
        int saved_fp = pvm->fp;
        int saved_sp = pvm->sp;
        Closure* saved_closure = pvm->current_closure;

        // Push arguments onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        if (!vm_bind_args(pvm, fn, proc, nargs))
        {
            pvm->sp = saved_sp;
            pvm->ip = saved_ip;
            return NULL;
        }

        // Set up frame: [args..., ret_addr, saved_fp, fn]
        vm_push_ip(pvm, NULL);               // NULL return address = exit sentinel
        vm_push(pvm, MAKE_NUM(pvm->fp));     // saved fp
        vm_push(pvm, fn);                    // callee (see frame_arity)
        pvm->fp = pvm->sp;
        pvm->ip = proc->chunk.code;
        pvm->current_closure = closure;      // set for upvalue access

        // Execute bytecode until ip becomes NULL (from RET with NULL return addr)
        pvm->exec_depth++;
        while (pvm->ip != NULL && !pvm->abort_exec)
        {
            VMExecResult result = vm_exec_one(pvm);
            if (result == VM_HALT || result == VM_ERROR)
                break;
        }

        // Result is on stack
        Object* result = pvm->abort_exec ? NULL : vm_pop(pvm);

        // Restore state. After an abort or escape, frames above saved_sp are
        // abandoned, so close their upvalues (a normal return already has).
        vm_close_upvalues(pvm, saved_sp);
        pvm->ip = saved_ip;
        pvm->fp = saved_fp;
        pvm->sp = saved_sp;
        pvm->current_closure = saved_closure;
        vm_exec_leave(pvm);

        return result;
    }
    else if (IS_PROCEDURE(fn))
    {
        // Scheme procedure - need to execute bytecode
        Procedure* proc = (Procedure*)fn;

        // Save current VM state
        VMCode_t* saved_ip = pvm->ip;
        int saved_fp = pvm->fp;
        int saved_sp = pvm->sp;
        Closure* saved_closure = pvm->current_closure;

        // Push arguments onto stack (in order - first arg pushed first)
        for (int i = 0; i < nargs; i++)
            vm_push(pvm, args[i]);

        if (!vm_bind_args(pvm, fn, proc, nargs))
        {
            pvm->sp = saved_sp;
            pvm->ip = saved_ip;
            return NULL;
        }

        // Set up frame: [args..., ret_addr, saved_fp, fn]
        vm_push_ip(pvm, NULL);               // NULL return address = exit sentinel
        vm_push(pvm, MAKE_NUM(pvm->fp));     // saved fp
        vm_push(pvm, fn);                    // callee (see frame_arity)
        pvm->fp = pvm->sp;
        pvm->ip = proc->chunk.code;
        pvm->current_closure = NULL;         // not in a closure

        // Execute bytecode until ip becomes NULL (from RET with NULL return addr)
        pvm->exec_depth++;
        while (pvm->ip != NULL && !pvm->abort_exec)
        {
            VMExecResult result = vm_exec_one(pvm);
            if (result == VM_HALT || result == VM_ERROR)
                break;
        }

        // Result is on stack
        Object* result = pvm->abort_exec ? NULL : vm_pop(pvm);

        // Restore state. After an abort or escape, frames above saved_sp are
        // abandoned, so close their upvalues (a normal return already has).
        vm_close_upvalues(pvm, saved_sp);
        pvm->ip = saved_ip;
        pvm->fp = saved_fp;
        pvm->sp = saved_sp;
        pvm->current_closure = saved_closure;
        vm_exec_leave(pvm);

        return result;
    }

    yyerror(pvm->sk_state, "vm_call_function: not a function");
    return NULL;
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
// Uses computed gotos on GCC/Clang for faster dispatch,
// Uses function pointer table dispatch for portable, clean execution.
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

    pvm->ip = pvm->abort_exec ? vm_halt_code : addr;
    pvm->exec_depth++;

    // Function pointer table dispatch - portable across all compilers.
    // An abort (vm_abort) moves ip to OP_HALT, so the loop needs no flag test.
    for (;;)
    {
        VMCode_t opcode = *pvm->ip++;

        if (g_bProfile && opcode < OP_LAST) {
            pvm->opcode_counts[opcode]++;
            pvm->total_opcodes++;
        }

        VMExecResult result = op_dispatch[opcode](pvm);
        if (result != VM_CONTINUE)
            break;
    }

    vm_exec_leave(pvm);

    if (g_bProfile)
        pvm->exec_time_ms += profile_get_ms() - start;
}

//---------------------------------------------------------------
// Print profiling report
//---------------------------------------------------------------
static const char* opcode_names[] = {
    "HALT", "PUSH", "POP", "PUSH_IP", "POP_IP", "PUSH_FP", "POP_FP",
    "PUSH_SP", "POP_SP", "NUM", "STRING", "TRUE", "FALSE",
    "STORE", "DUP", "LOAD", "RET", "CALL", "ICALL", "XCALL",
    "TAILCALL", "TAILCALL_I", "BRA", "BNT", "BNF", "NOP",
    "MAKE_CLOSURE", "GET_UPVALUE", "SET_UPVALUE", "LIST",
    "GET_LOCAL", "SET_LOCAL", "PRINT_TOS",
    "ADD", "SUB", "MUL", "LT", "LE", "EQ_NUM",
    "NUM_0", "NUM_1", "NUM_2",
    "SYMBOL"
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
        vm_check_alloc(pvm, pvm->grayStack);
    }

    pvm->grayStack[pvm->grayCount++] = object;
}


//--------------------------------------------------------------
// Mark the objects a chunk's code refers to: pushed constants (quoted
// data, procedures), called procedures and global keys. A LOAD_GLOBAL
// cache is skipped: it is only used while the global still holds the
// value, and a stale one may point to a freed object.
//--------------------------------------------------------------
static void markChunk(VM* pvm, CodeChunk* chunk)
{
    size_t p = 0;
    while (p < chunk->count)
    {
        VMCode_t op = chunk->code[p];
        assert(op < OP_LAST && "markChunk: lost track of the instructions");
        if (op >= OP_LAST)
            return;

        if ((op == OP_PUSH || op == OP_CALL || op == OP_TAILCALL || op == OP_LOAD_GLOBAL) &&
            p + 1 < chunk->count)
            markObject(pvm, (Object*)chunk->code[p + 1]);

        p += 1 + op_operand_words(op);
    }
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

    // mark open upvalues: the VM still links them, even if no closure does
    for (Upvalue* up = pvm->open_upvalues; up; up = up->next_open)
        markObject(pvm, (Object*)up);

    // procedures held by the VM between steps of a call
    markObject(pvm, pvm->pending_call);
    markObject(pvm, pvm->call_root);

    // an escape in progress (call/cc)
    markObject(pvm, pvm->escape_to);
    markObject(pvm, pvm->escape_value);

    // top-level code being compiled or run, and procedures being compiled
    for (int i = 0; i < pvm->root_chunk_count; i++)
        markChunk(pvm, pvm->root_chunks[i]);
    for (int i = 0; i < pvm->compile_root_count; i++)
        markObject(pvm, pvm->compile_roots[i]);

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
            free_chunk_strings(&proc->chunk);
            vm_alloc(pvm, proc->chunk.code, proc->chunk.capacity * sizeof(VMCode_t), 0);
            free(proc->chunk.lines);
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
            vm_alloc(pvm, closure->upvalues, sizeof(Upvalue*) * closure->upvalue_count, 0);
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
    case OBJ_SYMBOL:
        // Strings and symbols have no references to mark
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

            // mark the objects its code refers to
            markChunk(pvm, &proc->chunk);

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
                    markObject(pvm, (Object*)closure->upvalues[i]);
                }
            }
		}
        break;

    case OBJ_UPVALUE:
        // an open upvalue's value is on the stack, which is marked anyway
        markObject(pvm, ((Upvalue*)object)->closed);
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
static void removeWhiteFromTable(HashTable* table) {
    size_t index = 0;
    ht_key_t key;
    ht_value_t value;
    int SK_UNUSED removed = 0;

    // only remove unmarked strings
    while(ht_next(table, &index, &key, &value))
    {
        if (!IS_OBJ((Object*)value) || IS_MARKED((Object*)value))
            continue;

        if (HT_OK == ht_remove(table, key))
        {
            removed++;
            GC_LOG("-- gc removing interned string: '%s'\n", ((String*)value)->value);
        }
        else
            fprintf(stderr, "Error removing interned string during GC\n");
    }

    GC_LOG("-- gc removing (%d) white objects from intern table\n", removed);
}

// The intern tables are weak: they do not keep strings and symbols alive.
static void removeWhiteObjects(VM* pvm) {
    removeWhiteFromTable(pvm->interned_strings);
    removeWhiteFromTable(pvm->interned_symbols);
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
// vm_push, vm_pop, vm_peek, vm_fp_peek are now static inline in vm.h

//--------------------------------------------------------------
// A runtime error that cannot be continued from (stack overflow, unbound
// variable, ...). Report it once and abort the current run; vm_exec
// unwinds back to the top level.
//--------------------------------------------------------------
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline, cold))
#endif
void vm_stack_overflow(VM* pvm)
{
    vm_abort(pvm, "stack overflow (recursion too deep)");
}

//--------------------------------------------------------------
// Stop the run, as vm_abort does, after an error that was already
// reported: a native's runtime error (see sk_log)
//--------------------------------------------------------------
void vm_stop(VM* pvm)
{
    pvm->abort_exec = 1;
    pvm->ip = vm_halt_code;
}

//--------------------------------------------------------------
// A failure the VM cannot recover from, such as memory running out. The
// embedder's fatal handler may longjmp back to its own loop (and should
// then reset or recreate the interpreter); without one, or if it returns,
// print the message and exit.
//--------------------------------------------------------------
void vm_fatal(VM* pvm, const char* msg)
{
    if (pvm && pvm->fatal_handler)
        pvm->fatal_handler(pvm->sk_state, msg);

    fprintf(stderr, "FATAL: %s\n", msg);
    exit(1);
}

void* vm_check_alloc(VM* pvm, void* p)
{
    if (!p)
        vm_fatal(pvm, "out of memory");
    return p;
}

void vm_abort(VM* pvm, const char* msg)
{
    if (!pvm->abort_exec)
        yyerror(pvm->sk_state, msg);

    pvm->abort_exec = 1;

    // Stop at the next dispatch rather than testing abort_exec before every
    // instruction. Ops that set ip themselves (calls, returns) test it.
    pvm->ip = vm_halt_code;
}

//--------------------------------------------------------------
// Leave a vm_exec / vm_call_function. Once the outermost one finishes
// after an abort, discard the half-finished computation: clear the
// stack and frames, and accept new work.
//--------------------------------------------------------------
static void vm_exec_leave(VM* pvm)
{
    pvm->exec_depth--;

    // (an escape to a continuation is still on its way to its call/cc)
    if (pvm->exec_depth == 0 && pvm->abort_exec && !pvm->escape_to)
    {
        vm_close_upvalues(pvm, 0);
        pvm->sp = 0;
        pvm->fp = 0;
        pvm->current_closure = NULL;
        pvm->abort_exec = 0;
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

    if (!new_ptr && new_size > old_size) {
        // collect and try once more before giving up
        vm_gc(pvm);
        new_ptr = realloc(ptr, new_size);
    }
    if (!new_ptr)
        vm_fatal(pvm, "out of memory");

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

    // Over the heap limit even after collecting: stop the run, like a stack
    // overflow, so the session goes on (the allocation itself succeeds).
    // Only while code runs: an abort outside a run would cancel the next one.
    if (pvm->heap_limit && pvm->allocatedBytes > pvm->heap_limit &&
        pvm->exec_depth > 0 && !pvm->abort_exec)
    {
        vm_gc(pvm);
        if (pvm->allocatedBytes > pvm->heap_limit)
            vm_abort(pvm, "out of memory (heap limit reached)");
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
// Create a new symbol object (like string but OBJ_SYMBOL type)
//--------------------------------------------------------------
Object *vm_new_symbol(VM* pvm, const char* name)
{
    if (!pvm || !name) return NULL;

    // symbols are interned: one object per name, so eq? can compare pointers
    ht_value_t existing = ht_find(pvm->interned_symbols, name);
    if (existing)
        return (Object*)existing;

    Object* obj = vm_newObject(pvm, sizeof(String) + strlen(name) + 1, OBJ_SYMBOL);
    String* sym = (String*)obj;
    strcpy((char*)sym->value, name);

    ht_insert(pvm->interned_symbols, sym->value, sym);
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
    chunk->lines = NULL;
    chunk->line_count = 0;
    chunk->line_capacity = 0;
}

//--------------------------------------------------------------
// The number of operand words that follow an opcode (as the op_
// handlers read them)
//--------------------------------------------------------------
static int op_operand_words(VMCode_t op)
{
    switch (op)
    {
    case OP_PUSH: case OP_NUM: case OP_STRING: case OP_SYMBOL:
    case OP_ICALL: case OP_XCALL: case OP_TAILCALL_I: case OP_TAILCALL_X:
    case OP_BRA: case OP_BNT: case OP_BNF:
    case OP_MAKE_CLOSURE: case OP_GET_UPVALUE: case OP_SET_UPVALUE:
    case OP_CAPTURE_LOCAL: case OP_CAPTURE_UPVALUE:
    case OP_LIST: case OP_LIST_TAIL: case OP_GET_LOCAL: case OP_SET_LOCAL:
        return 1;
    case OP_CALL: case OP_TAILCALL:     // procedure, argument count
        return 2;
    case OP_LOAD_GLOBAL:                // key, cached value, cache version
        return 3;
    default:
        return 0;
    }
}

//--------------------------------------------------------------
// Free the C strings that OP_STRING and OP_SYMBOL operands own
//--------------------------------------------------------------
static void free_chunk_strings(CodeChunk* chunk)
{
    for (size_t p = 0; p < chunk->count; p += 1 + op_operand_words(chunk->code[p]))
    {
        VMCode_t op = chunk->code[p];
        if ((op == OP_STRING || op == OP_SYMBOL) && p + 1 < chunk->count)
        {
            free((void*)chunk->code[p + 1]);
            chunk->code[p + 1] = 0;
        }
    }
}

//--------------------------------------------------------------
// GC roots outside the object graph (see VM.root_chunks)
//--------------------------------------------------------------
void vm_push_root_chunk(VM* pvm, CodeChunk* chunk)
{
    if (pvm->root_chunk_count == pvm->root_chunk_capacity)
    {
        pvm->root_chunk_capacity = GROW_CAPACITY(pvm->root_chunk_capacity);
        pvm->root_chunks = (CodeChunk**)realloc(pvm->root_chunks, sizeof(CodeChunk*) * pvm->root_chunk_capacity);
        vm_check_alloc(pvm, pvm->root_chunks);
    }
    pvm->root_chunks[pvm->root_chunk_count++] = chunk;
}

void vm_pop_root_chunk(VM* pvm)
{
    assert(pvm->root_chunk_count > 0 && "root chunk underflow");
    pvm->root_chunk_count--;
}

void vm_push_compile_root(VM* pvm, Object* obj)
{
    if (pvm->compile_root_count == pvm->compile_root_capacity)
    {
        pvm->compile_root_capacity = GROW_CAPACITY(pvm->compile_root_capacity);
        pvm->compile_roots = (Object**)realloc(pvm->compile_roots, sizeof(Object*) * pvm->compile_root_capacity);
        vm_check_alloc(pvm, pvm->compile_roots);
    }
    pvm->compile_roots[pvm->compile_root_count++] = obj;
}

void vm_pop_compile_root(VM* pvm)
{
    assert(pvm->compile_root_count > 0 && "compile root underflow");
    pvm->compile_root_count--;
}

//--------------------------------------------------------------
// Free a code chunk
//--------------------------------------------------------------
void vm_free_chunk(VM* pvm, CodeChunk* chunk)
{
    free_chunk_strings(chunk);
	vm_alloc(pvm, chunk->code, chunk->capacity * sizeof(VMCode_t), 0);
	chunk->code = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
    free(chunk->lines);
    chunk->lines = NULL;
    chunk->line_count = chunk->line_capacity = 0;
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
    pvm->globals_version++;     // invalidate OP_LOAD_GLOBAL caches

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
    pvm->globals_version++;     // invalidate OP_LOAD_GLOBAL caches
    return ht_add(pvm->global_table, key, value);
}

//--------------------------------------------------------------
// Get a global object by key
//--------------------------------------------------------------
Object *vm_get_global(VM *pvm, Object* key)
{
    ht_value_t val = NULL;
    if (ht_lookup(pvm->global_table, key, &val) == HT_OK)
        return (Object*)val;
    return NULL;
}

//--------------------------------------------------------------
// Is there a global table entry (possibly the unbound placeholder) for key?
//--------------------------------------------------------------
int vm_has_global(VM *pvm, Object* key)
{
    ht_value_t val = NULL;
    return ht_lookup(pvm->global_table, key, &val) == HT_OK;
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
            if (ht_lookup(env->bindings, key, &val) == HT_OK)
                return (Object*)val;
            env = env->parent;
        }
	}

    return NULL;
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
            if (ht_lookup(env->bindings, key, &val) == HT_OK)
            {
                if (scope_depth) *scope_depth = depth;
                return (Object*)val;
            }
            env = env->parent;
            depth++;
        }
	}

    if (scope_depth) *scope_depth = -1;
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

