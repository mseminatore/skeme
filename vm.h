#ifndef __VM_H
#define __VM_H

#include <stdio.h>

#define VM_OK 1
#define VM_ERR -1

// configuration
// #define VM_USE_FREE_LIST
#define VM_STRESS_GC
#define VM_DEBUG_LOG_GC

#define VM_STACK_SIZE 256
#define VM_FREE_LIST 128
#define VM_MAX_GLOBALS 64
#define VM_INITIAL_GC_THRESHOLD 256
#define VM_GC_HEAP_GROWTH_FACTOR 2

#ifdef _DEBUG
#   define CHECK_THAT(cond)            assert(cond); if (!(cond)) return VM_ERR;
#else
#   define CHECK_THAT(cond)            if (!(cond)) return VM_ERR;
#endif

#ifdef VM_DEBUG_LOG_GC
#   define VM_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#   define VM_LOG(...)
// #   define VM_LOG __noop
#endif

// macros to check and convert to object pointers
#define IS_OBJ(obj) ((obj) != NULL && !((obj) & 0x2))
#define AS_OBJ(ObjT, obj) (IS_OBJ(obj) ? (ObjT*)(obj) : NULL)

// object types
enum {
    OBJ_INT,
    OBJ_STRING,
    OBJ_PAIR,
    OBJ_CLOSURE,
    OBJ_NATIVE_FN,
    OBJ_BUILTIN_FN,
    OBJ_NIL,
    OBJ_TRUE,
    OBJ_FALSE
};

// opcode definitions
enum { OP_PUSH, OP_POP, OP_NUM, OP_RET, OP_HALT /*OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_PRINT */};

typedef uint32_t VMCode_t;

//
// Define the Object structure
//
typedef struct Object {
    int type;               // Type of the object (e.g., integer, string, etc.)
    int marked;             // Mark flag for garbage collection
    struct Object* next;    // Pointer to the next object in the linked list
    struct Object* car;
    struct Object* cdr;
} Object;

// Define the VM structure
typedef struct VM {
    int sp;                         // Stack pointer
    Object* head;                   // Head of the object list
    Object* stack[VM_STACK_SIZE];   // Stack of object pointers
    int numObjects;                 // Number of allocated objects
    // size_t peakObjects;             // Peak number of allocated objects
    size_t nextGC;                  // Threshold to trigger next GC
    size_t allocatedBytes;          // Total allocated bytes

    int globals_count;              // Number of global objects
    Object globals[VM_MAX_GLOBALS]; // Head of the global objects list

    int grayCount;
    int grayCapacity;
    Object** grayStack;

#ifdef VM_USE_FREE_LIST
    int freeObjects;      // Number of free objects in the free list
    Object *freeList[VM_FREE_LIST];    // Free list for object reuse
#endif
} VM;

// function decls
int vm_init(VM* vm);
void vm_gc(VM* vm);
void vm_push(VM* vm, Object* obj);
Object* vm_pop(VM* vm);
Object* vm_peek(VM* vm, int distance);
void *vm_alloc(VM* vm, void *ptr, size_t old_size, size_t new_size);
Object* vm_newObject(VM* vm);
void vm_freeObjects(VM* vm);
void vm_emit(VM* vm, VMCode_t opcode);
void vm_exec(VM* vm, int addr);

#endif // __VM_H
