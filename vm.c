// vm.c
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "vm.h"

// TODO - should these be inside the VM struct?
static int vm_code_ptr = 0;
static VMCode_t vm_code[VM_MAX_CODE];

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

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
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->nextGC = VM_INITIAL_GC_THRESHOLD; // initial threshold
    pvm->allocatedBytes = 0;
    pvm->globals_count = 0;
    pvm->gc_cycles = 0;

	pvm->global_table = ht_create();
	ht_set_hash_func(pvm->global_table, HT_HASH_STRING);
	ht_set_compare_func(pvm->global_table, compare_fn);

    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;

    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->grayStack = NULL;

    // init globals
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
// Emit a bytecode instruction
//--------------------------------------------------------------
void vm_emit(VM* pvm, VMCode_t opcode) 
{
    if (vm_code_ptr >= VM_MAX_CODE) {
        fprintf(stderr, "VM code buffer overflow!\n");
        exit(1);
    }
    vm_code[vm_code_ptr++] = opcode;
}

//--------------------------------------------------------------
// Clear the VM code buffer
//--------------------------------------------------------------
void vm_clear_code()
{
    vm_code_ptr = 0;
}

//--------------------------------------------------------------
// Reset the VM
//--------------------------------------------------------------
int vm_reset(VM* pvm)
{
    CHECK_THAT(pvm != NULL);
    vm_clear_stack(pvm);
    vm_clear_code();
    return VM_OK;
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
//---------------------------------------------------------------
void vm_exec(VM* pvm, int addr)
{
    Object* obj;

    if (addr < 0 || addr >= vm_code_ptr) {
        fprintf(stderr, "Invalid VM code address: %d\n", addr);
        return;
    }

    VM_LOG("\n---- VM Execution Start ----\n");

    while(1)
    {
        VMCode_t opcode = vm_code[addr++];

        switch(opcode)
        {
        case OP_PUSH:
            VM_LOG("PUSH\n");
            vm_push(pvm, (Object*)vm_code[addr++]);
            break;

        case OP_POP:
            VM_LOG("POP\n");
            break;

        case OP_NUM:
            VM_LOG("NUM\n");
            vm_push(pvm, MAKE_NUM(vm_code[addr++]));
            break;

        case OP_STRING:
            VM_LOG("STRING\n");
            obj = vm_newString(pvm, (const char*)vm_code[addr++]);
            vm_push(pvm, obj);
            break;

        case OP_RET:
            VM_LOG("RET\n");
            // TODO - set IP to return address
            break;

        case OP_HALT:
            VM_LOG("HALT\n");
            return;

        case OP_TRUE:
			vm_pushBoolean(pvm, TRUE);
            break;

        case OP_FALSE:
			vm_pushBoolean(pvm, FALSE);
            break;

        case OP_NATIVE:
            {
                VM_LOG("NATIVE FN\n");
                SKFunc func = (SKFunc)vm_code[addr++];
                if (func) {
                    func((SKState*)1); // In a real implementation, pass the VM or context
                } else {
                    fprintf(stderr, "NULL function pointer in NATIVE call!\n");
                }
		    }
            break;

        default:
            printf("Unknown opcode: %d\n", addr);
            break;
        }
    }
}

//--------------------------------------------------------------
// Mark an object
//--------------------------------------------------------------
static void markObject(VM* pvm, Object* object) {
    if (!IS_OBJ(object)) return;

    if (/*object == NULL || */ object->marked) return;

    object->marked = 1;

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
    VM_LOG("-- gc marking\n");
    // Recursively mark all reachable objects

    // mark global objects
    for (int i = 0; i < pvm->globals_count; i++) 
    {
        markObject(pvm, pvm->globals[i]);
    }

    // mark stack objects
    for (int i = 0; i < pvm->sp; i++) 
    {
        markObject(pvm, pvm->stack[i]);
    }
}

//--------------------------------------------------------------
// Sweep unmarked objects
//--------------------------------------------------------------
static int sweepObjects(VM* pvm) {
    VM_LOG("-- gc sweeping\n");

    int free_count = 0;

    Object** obj = &pvm->head;
    while (*obj) 
    {
        if (!(*obj)->marked) 
        {
            Object* unreached = *obj;
            *obj = unreached->next;

            vm_alloc(pvm, unreached, unreached->size, 0);
            pvm->numObjects--;
            free_count++;
        } else 
        {
            (*obj)->marked = 0; // Reset mark for next GC cycle
            obj = &(*obj)->next;
        }
    }

    VM_LOG("-- gc (%d) objects freed\n", free_count);

    return free_count;
}

//-------------------------------------------------------------
// Mark an object and add it to the gray stack
//-------------------------------------------------------------
static void blackenObject(VM* pvm, Object* object) 
{
    // In a real implementation, you would mark the object's references
    // and add them to the gray stack if they are not already marked
    VM_LOG("-- gc blackening object\n");

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
    VM_LOG("-- gc tracing references\n");

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
    // In a real implementation, you would remove unmarked strings
    // from the interned string table here
    VM_LOG("-- gc removing white objects from intern table\n");
}

//--------------------------------------------------------------
// Perform garbage collection
// Mark-and-sweep algorithm
//--------------------------------------------------------------
void vm_gc(VM* pvm) {
    VM_LOG("-- gc begin\n");

    size_t before = pvm->allocatedBytes;

    markObjects(pvm);

    // if there are any references from the stack or globals, we need to
    // follow those references and mark them as well
    traceReferences(pvm);

    // remove strings from the intern table that are not marked
    removeWhiteObjects(pvm);

    int freed = sweepObjects(pvm);

    pvm->gc_cycles++;
    
    VM_LOG("-- gc end [%d / %d] objs freed/live, [%zu / %zu] bytes freed/alloc'd, next at %zu.\n", freed, pvm->numObjects, before - pvm->allocatedBytes, pvm->allocatedBytes, pvm->nextGC);
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
Object* vm_newObject(VM* pvm, size_t size) {
    if (size == 0)
        size = sizeof(Object);

    Object* obj = (Object*)vm_alloc(pvm, NULL, 0, size);
    obj->marked = 0;        // new objects are unmarked
    obj->type = 0;          // default type
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
// Create a new string object
//--------------------------------------------------------------
Object *vm_newString(VM* pvm, const char* str)
{
    Object* obj = vm_newObject(pvm, sizeof(String));
    String* stringObj = (String*)obj;

    stringObj->base.type = OBJ_STRING;
    stringObj->value = str;
    return obj;
}

//--------------------------------------------------------------
// Define a global object
//--------------------------------------------------------------
void vm_define_global(VM *pvm, Object* key, Object *value)
{
    ht_insert(pvm->global_table, key, value);
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


