//--------------------------------------------------------------
// vm.c
//--------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "vm.h"

int g_bGCLog = FALSE;
int g_bVMLog = FALSE;
int g_bGCStress = FALSE;

#define GC_LOG(...) (void)(g_bGCLog && fprintf(stderr, __VA_ARGS__))
#define VM_LOG(...) (void)(g_bVMLog && fprintf(stderr, __VA_ARGS__))

CodeChunk* g_current_chunk = NULL;

//--------------------------------------
// Get the current code chunk
//--------------------------------------
CodeChunk* vm_get_current_chunk()
{
	return g_current_chunk;
}

//--------------------------------------
//
//--------------------------------------
CodeChunk *vm_set_current_chunk(CodeChunk* chunk)
{
    assert(chunk);

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
    CHECK_THAT(pvm != NULL);

    pvm->sp = 0;
    pvm->ip = 0;
    pvm->return_sp = 0;
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->nextGC = VM_INITIAL_GC_THRESHOLD; // initial threshold
    pvm->allocatedBytes = 0;
    pvm->gc_cycles = 0;

	// init globals - since we use pointer keys, use default hash and compare funcs
    pvm->global_table = ht_create();

	// init interned strings table
    pvm->interned_strings = ht_create();
    ht_set_hash_func(pvm->interned_strings, HT_HASH_STRING);
    ht_set_compare_func(pvm->interned_strings, compare_fn);

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

    return VM_OK;
}

//--------------------------------------------------------------
// Shutdown the VM and free all resources
//--------------------------------------------------------------
int vm_shutdown(VM* pvm)
{
    CHECK_THAT(pvm != NULL);

    vm_freeObjects(pvm);

    free(pvm->grayStack);
    pvm->grayStack = NULL;

	ht_free(pvm->global_table);
	pvm->global_table = NULL;

    ht_free(pvm->interned_strings);
	pvm->interned_strings = NULL;

    return VM_OK;
}

//--------------------------------------------------------------
// Clear the VM stack
//--------------------------------------------------------------
int vm_clear_stack(VM* pvm)
{
    CHECK_THAT(pvm != NULL);

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
    return chunk->count-1;

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
void vm_emit_object(VM* pvm, Object* obj)
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
    CHECK_THAT(pvm != NULL);

    vm_clear_stack(pvm);
    vm_clear_code(pvm);
    
    return VM_OK;
}

//---------------------------------------------------------------
// Disassemble a code chunk for debugging
//---------------------------------------------------------------
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name)
{
    printf("== Disassembly of %s ==\n", name);

    VMCode_t* ip = chunk->code;

    for (; ip < &chunk->code[chunk->count];)
    {
        printf("%p ", ip);
        VMCode_t instruction = *ip++;

        switch (instruction)
        {
        case OP_PUSH:
            printf("PUSH %p\n", (VMCode_t*)*ip++);
            break;

        case OP_POP:
            printf("POP\n");
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

        case OP_NATIVE:
            printf("CALL NATIVE FN\n");
            ip++;  // skip native function pointer
            break;

        case OP_RET:
            printf("RET\n");
            break;

        case OP_CALL:
        {
            Procedure* proc = (Procedure*)*ip;
            printf("CALL %p\n", proc->chunk.code);
            ip++;
        }
            break;

        case OP_NOP:
            printf("NOP\n");
			break;

        case OP_GOTO:
            printf("GOTO %p\n", (VMCode_t*)*ip++);
            break;

        case OP_BNT:
            printf("BNT %p\n", (VMCode_t*)*ip++);
            break;

        case OP_BNF:
            printf("BNF %p\n", (VMCode_t*)*ip++);
            break;

        case OP_HALT:
            printf("HALT\n");
            break;

        case OP_LIST:
            printf("MAKE_LIST\n");
            break;

        default:
            printf("Unknown opcode: %zd\n", (VMCode_t)*ip++);
            break;
        }
    }
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
//---------------------------------------------------------------
void vm_exec(VM* pvm, VMCode_t *addr)
{
    Object* obj;

    if (pvm == NULL) {
        fprintf(stderr, "VM is NULL!\n");
        return;
    }
    
    if (!addr) {
        fprintf(stderr, "Invalid VM code address: %p\n", addr);
        return;
    }

    VM_LOG("\n---- VM Execution Trace ----\n");

    pvm->ip = addr;

    while(1)
    {
        VM_LOG("IP=%p SP=%d: ", pvm->ip, pvm->sp);

        VMCode_t opcode = *pvm->ip++;

        switch(opcode)
        {
        case OP_PUSH:
            VM_LOG("PUSH obj\n");
            vm_push(pvm, (Object*)*pvm->ip++);
            break;

        case OP_POP:
            VM_LOG("POP obj\n");
            vm_pop(pvm);
            break;

        case OP_DUP:
            VM_LOG("DUP\n");
            obj = vm_peek(pvm, 0);
            vm_push(pvm, obj);
			break;

        case OP_NUM:
            VM_LOG("PUSH NUM %zu\n", *pvm->ip);
            vm_push(pvm, MAKE_NUM(*pvm->ip++));
            break;

        case OP_STRING:
            VM_LOG("PUSH STRING '%s'\n", (const char*)*pvm->ip);
            obj = vm_new_string(pvm, (const char*)*pvm->ip++);
            vm_push(pvm, obj);
            break;

        case OP_RET:
            VM_LOG("RET\n");

            // set IP to return address
            pvm->ip = vm_pop_ip(pvm);
            break;

        case OP_HALT:
            VM_LOG("HALT\n");
            return;

        case OP_TRUE:
            VM_LOG("PUSH TRUE\n");
            vm_pushBoolean(pvm, TRUE);
            break;

        case OP_FALSE:
            VM_LOG("PUSH FALSE\n");
            vm_pushBoolean(pvm, FALSE);
            break;

        case OP_CALL:
        {
            Procedure* proc = (Procedure*)*pvm->ip;
            VM_LOG("CALL %p\n", proc->chunk.code);

            Object* n = vm_pop(pvm);
        	int nargs = AS_NUM(n);

            if (proc->arity != nargs)
            {
                yyerror("incorrect number of arguments to function");
                // clear args from stack
                vm_pop_n(pvm, nargs);
                break;
            }

            // call function by setting IP to function address
            vm_push_ip(pvm, pvm->ip + 1);   // push return address onto stack

            pvm->ip = proc->chunk.code;
        }
            break;

        case OP_BNT:
            VM_LOG("BNT %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            obj = vm_pop(pvm);
            if (IS_FALSE(obj)) 
            {
                // branch to new address
                pvm->ip += *pvm->ip;
            }
            else 
            {
                // move to next instruction in sequence
                pvm->ip++;
            }
            break;

        case OP_BNF:
            VM_LOG("BNF %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            obj = vm_pop(pvm);
            if (!IS_FALSE(obj))
            {
                // branch to new address
                pvm->ip += *pvm->ip;
            }
            else
            {
                // move to next instruction in sequence
                pvm->ip++;
            }
            break;

        case OP_NOP:
            VM_LOG("NOP\n");
            // do nothing!
            break;

        case OP_GOTO:
        {
            VM_LOG("GOTO %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            pvm->ip += *pvm->ip;
        }
			break;

        case OP_NATIVE:
        {
            VM_LOG("CALL NATIVE FN\n");
            NativeFunction* proc = (NativeFunction*)*pvm->ip++;
            SKFunc func = (SKFunc)proc->func;

            if (func) 
            {
                int arity = proc->arity;
                if (arity >= 0)
                {
                    Object* n = vm_pop(pvm);
                    int nargs = AS_NUM(n);
                    if (nargs != arity)
                    {
                        yyerror("incorrect number of arguments to native function");
                        // clear stack
                        vm_pop_n(pvm, nargs);
                        break;
                    }
                }

                func((SKState*)1); // TODO - In a real implementation, pass the VM or context

            } else {
                fprintf(stderr, "NULL function pointer in NATIVE call!\n");
            }
		}
            break;

        case OP_STORE:
        {
            // variable assignment
            Object *value = vm_pop(pvm);
            Object *key = vm_pop(pvm);
            VM_LOG("STORE '%s'\n", ((String*)key)->value);
            vm_update_global(pvm, key, value);
        }
			break;

        case OP_LOAD:
        {
            Object* key = vm_pop(pvm);
            VM_LOG("LOAD '%s'\n", ((String*)key)->value);
            // variable retrieval
            vm_push(pvm, vm_get_global(pvm, key));
        }
            break;

        case OP_LIST:
        {
            VM_LOG("MAKE_LIST\n");
            Pair* alist = NULL;
            Pair* pair = NULL;

            obj = vm_pop(pvm);
			int nargs = AS_NUM(obj);
            for (int i = 0; i < nargs; i++)
            {
                vm_push(pvm, (Object*)pair);    // stash pair on stack to prevent premature GC
                alist = (Pair*)vm_newObject(pvm, sizeof(Pair), OBJ_PAIR);
                vm_pop(pvm);                    // discard stashed pair
                alist->car = vm_pop(pvm);
                alist->cdr = (Object*)pair;
                pair = alist;
            }

			// push the constructed list onto the stack
            vm_push(pvm, (Object*)alist);
        }
            break;

        default:
            printf("Unknown opcode: %zu\n", opcode);
            break;
        }
    }
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
}

//--------------------------------------------------------------
// Free an object
//--------------------------------------------------------------
static void freeObject(VM* pvm, Object* object)
{
	GC_LOG("-- gc freeing object of type %d\n", OBJ_TYPE(object));

    if (!IS_OBJ(object))
		return;

	// clean up based on object type
    switch (OBJ_TYPE(object))
    {
    case OBJ_FUNCTION:
        {
            // free function-specific resources
            Procedure * proc = (Procedure*)object;
			vm_alloc(pvm, proc->chunk.code, proc->chunk.capacity * sizeof(VMCode_t), 0);
        }
		break;

    default:
        // nothing to do!
        break;
    }

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
        // Native functions have no references to mark
        break;

    default:
        // Handle other object types as needed
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
    int UNUSED removed = 0;

    // only remove unmarked strings
    while(ht_next(pvm->interned_strings, &index, &key, &value))
    {
        if (!IS_OBJ((Object*)value) || IS_MARKED((Object*)value))
            continue;

        if (HT_OK == ht_remove(pvm->interned_strings, key))
            removed++;
        else
            fprintf(stderr, "Error removing interned string during GC\n");

        //free((void*)key);
    }

    GC_LOG("-- gc removing (%d) white objects from intern table\n", removed);
}

//--------------------------------------------------------------
// Perform garbage collection
// Mark-and-sweep algorithm
//--------------------------------------------------------------
void vm_gc(VM* pvm) {
    GC_LOG("-- gc begin\n");

    size_t before UNUSED = pvm->allocatedBytes;

    markObjects(pvm);

    // if there are any references from the stack or globals, we need to
    // follow those references and mark them as well
    traceReferences(pvm);

    // remove strings from the intern table that are not marked
    removeWhiteObjects(pvm);

    int freed UNUSED = sweepObjects(pvm);

    pvm->gc_cycles++;
    
    GC_LOG("-- gc end [%d / %zu] objs freed/live, [%zu / %zu] bytes freed/alloc'd, next at %zu.\n", freed, pvm->numObjects, before - pvm->allocatedBytes, pvm->allocatedBytes, pvm->nextGC);
}

//--------------------------------------------------------------
// Push a return address onto the return stack
//--------------------------------------------------------------
void vm_push_ip(VM* pvm, VMCode_t *ip)
{
	if (pvm->return_sp >= VM_STACK_SIZE) {
		fprintf(stderr, "Return stack overflow!\n");
		return;
	}

	pvm->return_stack[pvm->return_sp++] = ip;
}

//--------------------------------------------------------------
// Pop a return address from the return stack
//--------------------------------------------------------------
VMCode_t* vm_pop_ip(VM* pvm) 
{
	if (pvm->return_sp <= 0) {
		fprintf(stderr, "Return stack underflow!\n");
		return NULL;
	}

	return pvm->return_stack[--pvm->return_sp];
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
#ifdef VM_STRESS_GC
        // Force a garbage collection on every allocation if stress GC is enabled
        // This is useful for testing purposes
        vm_gc(pvm);
#endif
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
    Object* obj = vm_newObject(pvm, sizeof(Procedure), OBJ_FUNCTION);

    Procedure* proc = AS_OBJ(Procedure, obj);
    if (!proc)
        return NULL;

    proc->arity = 0;

	vm_init_chunk(&proc->chunk);

    return obj;
}

//--------------------------------------------------------------
// Define a global object
//--------------------------------------------------------------
void vm_define_global(VM *pvm, Object* key, Object *value)
{
    // use ht_add() if we can redefine a global
    ht_insert(pvm->global_table, key, value);
}

//--------------------------------------------------------------
// Update a global object
//--------------------------------------------------------------
void vm_update_global(VM *pvm, Object* key, Object *value)
{
    ht_add(pvm->global_table, key, value);
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
// Free all objects in the VM
//--------------------------------------------------------------
void vm_freeObjects(VM* pvm) {
    Object* obj = pvm->head;

    while (obj) {
        Object* next = obj->next;
        free(obj);
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
}

