// vm.c
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "vm.h"

#define VM_MAX_CODE 1024

//
static int vm_code_ptr = 0;
static VMCode_t vm_code[VM_MAX_CODE];

// static Object *vm_globals[VM_MAX_GLOBALS];
// static int vm_globals_count = 0;

#define MAKE_NUM(val)   ((Object*)((uintptr_t)(val<<2)|0x1))

//--------------------------------------------------------------
// Initialize the VM
//--------------------------------------------------------------
int vm_init(VM* vm) {
    CHECK_THAT(vm != NULL);

    vm->sp = 0;
    vm->head = NULL;
    vm->numObjects = 0;
    vm->nextGC = VM_INITIAL_GC_THRESHOLD; // initial threshold
    vm->allocatedBytes = 0;
    vm->globals_count = 0;
    
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;

#ifdef VM_USE_FREE_LIST
    vm->freeObjects = 0;
#endif

    return VM_OK;
}

//--------------------------------------------------------------
// Emit a bytecode instruction
//--------------------------------------------------------------
void vm_emit(VM* vm, VMCode_t opcode) 
{
    if (vm_code_ptr >= VM_MAX_CODE) {
        fprintf(stderr, "VM code buffer overflow!\n");
        exit(1);
    }
    vm_code[vm_code_ptr++] = opcode;
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
//---------------------------------------------------------------
void vm_exec(VM* vm, int addr)
{
    if (addr < 0 || addr >= vm_code_ptr) {
        fprintf(stderr, "Invalid VM code address: %d\n", addr);
        return;
    }

    printf("\n---- VM Execution Start ----\n");

    while(1)
    {
        VMCode_t opcode = vm_code[addr++];

        switch(opcode)
        {
        case OP_PUSH:
            printf("PUSH\n");
            break;

        case OP_POP:
            printf("POP\n");
            break;

        case OP_NUM:
            printf("NUM\n");
            vm_push(vm, MAKE_NUM(vm_code[addr++]));
            break;

        case OP_RET:
            printf("RET\n");
            break;

        case OP_HALT:
            printf("HALT\n");
            return;

        default:
            printf("Unknown opcode: %d\n", addr);
            break;
        }
    }
}

//--------------------------------------------------------------
// Mark an object
//--------------------------------------------------------------
static void markObject(Object* object) {
    // if (!IS_OBJ(object)) return;

    if (object == NULL || object->marked) return;

    object->marked = 1;

    // if (vm->grayCapacity < vm->grayCount + 1) {
    //     vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
    //     vm->grayStack = (Object**)realloc(vm->grayStack, sizeof(Object*) * vm->grayCapacity);
    // }

    // vm->grayStack[vm->grayCount++] = object;
}

//--------------------------------------------------------------
// Mark all reachable objects
//--------------------------------------------------------------
static void markObjects(VM* vm) {
    VM_LOG("-- gc marking\n");
    // In a real implementation, you would recursively mark all reachable objects

    // mark roots: globals, stack, constants, etc.

    // mark global objects
    for (int i = 0; i < vm->globals_count; i++) {
        markObject(&vm->globals[i]);
        // if (vm->globals[i].marked == 0) {
        //     vm->globals[i].marked = 1;
        // }
    }

    // mark stack objects
    for (int i = 0; i < vm->sp; i++) {
        markObject(vm->stack[i]);
        // if (vm->stack[i]) {
        //     vm->stack[i]->marked = 1;
        // }
    }
}

//--------------------------------------------------------------
// Sweep unmarked objects
//--------------------------------------------------------------
static int sweepObjects(VM* vm) {
    VM_LOG("-- gc sweeping\n");

    int free_count = 0;

    Object** obj = &vm->head;
    while (*obj) {
        if (!(*obj)->marked) {
            Object* unreached = *obj;
            *obj = unreached->next;

            // free(unreached);
            vm_alloc(vm, unreached, sizeof(Object), 0);
            vm->numObjects--;
            free_count++;
        } else {
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
static void blackenObject(Object* object) {
    // In a real implementation, you would mark the object's references
    // and add them to the gray stack if they are not already marked
    VM_LOG("-- gc blackening object\n");
}

//--------------------------------------------------------------
// Trace references from the gray stack
//--------------------------------------------------------------
static void traceReferences(VM* vm) {
    // In a real implementation, you would trace references
    // from the gray stack and mark reachable objects
    VM_LOG("-- gc tracing references\n");

    while (vm->grayCount > 0) {
        Object* object = vm->grayStack[--vm->grayCount];
        blackenObject(object);
    }
}

//--------------------------------------------------------------
// Perform garbage collection
// Mark-and-sweep algorithm
//--------------------------------------------------------------
void vm_gc(VM* vm) {
    VM_LOG("-- gc begin\n");

    size_t before = vm->allocatedBytes;

    markObjects(vm);

    // if there are any references from the stack or globals, we need to
    // follow those references and mark them as well
    // traceReferences(vm);

    // remove strings from the intern table that are not marked
    // removeWhiteObjects(vm);

    int freed = sweepObjects(vm);

    VM_LOG("-- gc end [%d / %d] objs freed/live, [%zu / %zu] bytes freed/alloc'd, next at %zu.\n", freed, vm->numObjects, before - vm->allocatedBytes, vm->allocatedBytes, vm->nextGC);
}

//--------------------------------------------------------------
// Push an object onto the stack
//--------------------------------------------------------------
void vm_push(VM* vm, Object* obj) {
    if (vm->sp >= VM_STACK_SIZE) {
        fprintf(stderr, "Stack overflow!\n");
        return;
    }

    vm->stack[vm->sp++] = obj;
}

//--------------------------------------------------------------
// Pop an object from the stack
//--------------------------------------------------------------
Object* vm_pop(VM* vm) {
    if (vm->sp <= 0) {
        fprintf(stderr, "Stack underflow!\n");
        return NULL;
    }

    return vm->stack[--vm->sp];
}

//--------------------------------------------------------------
// Peek at an object on the stack without popping it
//--------------------------------------------------------------
Object* vm_peek(VM* vm, int distance) {
    if (vm->sp - 1 - distance < 0) {
        fprintf(stderr, "Stack underflow on peek!\n");
        return NULL;
    }

    return vm->stack[vm->sp - 1 - distance];
}

//--------------------------------------------------------------
// Custom realloc function for memory management
//--------------------------------------------------------------
void *vm_alloc(VM* vm, void *ptr, size_t old_size, size_t new_size) {
    // nothing to do!
    if (new_size == old_size) {
        fprintf(stderr, "Realloc called with same size!\n");
        return ptr;
    }

    if (new_size > old_size) {
#ifdef VM_STRESS_GC
        // Force a garbage collection on every allocation if stress GC is enabled
        // This is useful for testing purposes
        vm_gc(vm); // Uncomment and ensure vm is accessible here
#endif
    }

    // free the memory
    if (new_size == 0) {
        free(ptr);

        // TODO update allocatedBytes
        vm->allocatedBytes -= old_size;

        return NULL;
    }

    // Allocate more or less memory
    void *new_ptr = realloc(ptr, new_size);

    if (!new_ptr) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }

    // Update allocated bytes
    vm->allocatedBytes += new_size - old_size;

    // update gc threshold
    if (vm->allocatedBytes > vm->nextGC) {
        vm_gc(vm);
        vm->nextGC = vm->allocatedBytes * VM_GC_HEAP_GROWTH_FACTOR;
    }

    return new_ptr;
}

//--------------------------------------------------------------
// Create a new object and add it to the VM's object list
//--------------------------------------------------------------
Object* vm_newObject(VM* vm) {
    // if (vm->numObjects == VM_STACK_SIZE) {
    //     vm_gc(vm);
    // }

    Object* obj = (Object*)vm_alloc(vm, NULL, 0, sizeof(Object));
    obj->marked = 0;        // new objects are unmarked
    obj->type = 0;          // default type
    obj->car = NULL;
    obj->cdr = NULL;
    obj->next = vm->head;   // Insert at the head of the object list
    vm->head = obj;
    vm->numObjects++;
    return obj;
}

//--------------------------------------------------------------
// Free all objects in the VM
//--------------------------------------------------------------
void vm_freeObjects(VM* vm) {
    Object* obj = vm->head;

    while (obj) {
        Object* next = obj->next;
        free(obj);
        obj = next;
    }

    free(vm->grayStack);
    vm->grayStack = NULL;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->head = NULL;
    vm->numObjects = 0;
    vm->allocatedBytes = 0;
}


