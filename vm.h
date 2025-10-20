#ifndef __VM_H
#define __VM_H

#include <stdio.h>
#include <stdint.h> // Add this include at the top of the file, before typedefs
#include "hash/hash.h"

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

#define VM_MAX_CODE 1024

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

#ifdef _DEBUG
#   define CHECK_THAT(cond)            assert(cond); if (!(cond)) return VM_ERR;
#else
#   define CHECK_THAT(cond)            if (!(cond)) return VM_ERR;
#endif

#ifdef VM_DEBUG_LOG_GC
#   define VM_LOG(...) (fprintf(stderr, __VA_ARGS__))
#else
#   define VM_LOG(...)
// #   define VM_LOG __noop
#endif

// macros to check and convert to object pointers
#define IS_OBJ(obj) ((obj) != NULL && !((intptr_t)(obj) & 0x3))
#define AS_OBJ(ObjT, obj) (IS_OBJ(obj) ? (ObjT*)(obj) : NULL)
#define MAKE_NUM(val)   ((Object*)((uintptr_t)(val<<2) | 0x1))
#define AS_NUM(obj)    (IS_OBJ(obj) ? 0 : ((int)((intptr_t)(obj) >> 2)))
#define IS_NUM(obj)    (!IS_OBJ(obj) && (((intptr_t)(obj) & 0x3) == 0x1))
#define IS_BOOL(obj)   (IS_OBJ(obj) && AS_OBJ(Boolean, obj)->type == OBJ_BOOL)
#define IS_NIL(obj)    (obj == NULL)

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

// object types
enum {
    //OBJ_INT,
    OBJ_STRING,
    OBJ_PAIR,
    // OBJ_LIST,
	OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE_FN,
    //OBJ_BUILTIN_FN,
    // OBJ_NIL,
    OBJ_BOOL
};

// opcode definitions
enum { OP_PUSH, OP_POP, OP_NUM, OP_STRING, OP_TRUE, OP_FALSE, OP_FN, OP_NATIVE, OP_RET, OP_HALT /*OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_PRINT */};

typedef intptr_t VMCode_t;

//
// Define the Object structure
//
typedef struct Object {
    int type;               // Type of the object (e.g., integer, string, etc.)
    size_t size;            // Size of the object (for strings, pairs, etc.)
    int marked;             // Mark flag for garbage collection
    struct Object* next;    // Pointer to the next object in the linked list
} Object;

typedef struct Boolean {
	Object base;            // base object
    int value;              // 0 for false, 1 for true
} Boolean;

typedef struct Pair {
	Object base;    // base object
	Object* car;    // first element
	Object* cdr;    // second element
} Pair;

typedef struct String {
    Object base;        // base object
    const char* value;  // string value
} String;

// Define the VM structure
typedef struct VM {
	int sp;                         // Stack pointer should be size_t or uint
    VMCode_t ip;                    // Instruction pointer
    Object* head;                   // Head of the object list
    Object* stack[VM_STACK_SIZE];   // Stack of object pointers
	int numObjects;                 // Number of allocated objects should be size_t
    size_t peakObjects;             // Peak number of allocated objects
	size_t peakAllocatedBytes;      // Peak allocated bytes
    size_t nextGC;                  // Threshold to trigger next GC
    size_t allocatedBytes;          // Total allocated bytes
    size_t gc_cycles;               // Number of GC cycles run

	int globals_count;              // Number of global objects should be size_t or uint
    Object *globals[VM_MAX_GLOBALS]; // Head of the global objects list
	HashTable *global_table;        // Hash table for global variables

	// TODO - implement interned string table
    int grayCount;
    int grayCapacity;
    Object** grayStack;

#ifdef VM_USE_FREE_LIST
    int freeObjects;      // Number of free objects in the free list
    Object *freeList[VM_FREE_LIST];    // Free list for object reuse
#endif
} VM;

// function decls
int vm_init(VM* pvm);
int vm_shutdown(VM* pvm);
void vm_gc(VM* pvm);
void vm_push(VM* pvm, Object* obj);
Object* vm_pop(VM* pvm);
Object* vm_peek(VM* pvm, int distance);

void *vm_alloc(VM* pvm, void *ptr, size_t old_size, size_t new_size);
Object* vm_newObject(VM* pvm, size_t size);
Object* vm_newBoolean(VM* pvm, int val);
void vm_pushBoolean(VM* pvm, int val);
void vm_freeObjects(VM* pvm);
Object* vm_newString(VM* pvm, const char* str);

void vm_emit(VM* pvm, VMCode_t opcode);
void vm_exec(VM* pvm, int addr);
int vm_clear_stack(VM* pvm);
void vm_clear_code();
int vm_reset(VM* pvm);

#endif // __VM_H
