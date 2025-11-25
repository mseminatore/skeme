#ifndef __VM_H
#define __VM_H

#include <stdio.h>
#include <stdint.h> // Add this include at the top of the file, before typedefs
#include "hash/hash.h"

#define VM_OK 1
#define VM_ERR -1

// configuration
// #define VM_USE_FREE_LIST

// uncomment to enable GC stress testing (collect on every allocation)
#define VM_STRESS_GC

// uncomment to enable VM debug logging
 //#define VM_DEBUG_LOG

#define VM_STACK_SIZE 256
#define VM_FREE_LIST 128
#define VM_MAX_GLOBALS 64
#define VM_INITIAL_GC_THRESHOLD 256
#define VM_GC_HEAP_GROWTH_FACTOR 2

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define GROW_ARRAY(type, array, oldCount, newCount) (type*)vm_realloc(&vm, array, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#ifdef _WIN32
#   define UNUSED //__declspec(unused)
#else
#   define UNUSED __attribute__((unused))
#endif

// TODO - use dynamic code buffer
#define VM_MAX_CODE 1024

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

#ifdef _DEBUG
#   define CHECK_THAT(cond)            do { assert(cond); if (!(cond)) return VM_ERR; } while(0)
#else
#   define CHECK_THAT(cond)            do { if (!(cond)) return VM_ERR; } while(0)
#endif

//#ifdef VM_DEBUG_LOG
//#   define VM_LOG(...) (fprintf(stderr, __VA_ARGS__))
//#else
//#   define VM_LOG(...)
//// #   define VM_LOG __noop
//#endif

extern int g_bGCLog;
void yyerror(const char *s);

//-----------------------------
// Define the Object structure
//-----------------------------
typedef struct Object {
    uint8_t type;           // Type of the object (e.g., integer, string, etc.)
    uint8_t marked;         // Mark flag for garbage collection
    size_t size;            // Size of the object (for strings, pairs, etc.)
    struct Object* next;    // Pointer to the next object in the linked list
} Object;

// macros to check and convert to object pointers
#define IS_OBJ(obj)         ((obj) != NULL && !((intptr_t)(obj) & 0x3))
#define AS_OBJ(ObjT, obj)   (IS_OBJ(obj) ? (ObjT*)(obj) : NULL)
#define MAKE_NUM(val)       ((Object*)((uintptr_t)((val)<<2) | 0x1))
#define AS_NUM(obj)         (IS_OBJ(obj) ? 0 : ((int)((intptr_t)(obj) >> 2)))
#define IS_NUM(obj)         (!IS_OBJ(obj) && (((intptr_t)(obj) & 0x3) == 0x1))
#define IS_NIL(obj)         (obj == NULL)

#define OBJ_TYPE(obj)       (IS_OBJ(obj) ? AS_OBJ(Object, obj)->type : OBJ_NULL)
#define SET_TYPE(obj, t)    do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->type = (t); } while(0)

#define MARK_OBJ(obj)       do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->marked = 1; } while(0)
#define UNMARK_OBJ(obj)     do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->marked = 0; } while(0)
#define IS_MARKED(obj)      (IS_OBJ(obj) ? AS_OBJ(Object, obj)->marked : 0)

#define IS_BOOL(obj)    (!IS_OBJ(obj) && (((intptr_t)(obj) & 0x2)))
#define IS_TRUE(obj)    (((intptr_t)(obj)) == 3)
#define IS_FALSE(obj)   (((intptr_t)(obj)) == 2)
#define MAKE_TRUE()     ((Object*)3)
#define MAKE_FALSE()    ((Object*)2)

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

// object types
enum 
{
    OBJ_MARK_BIT,
    OBJ_NULL,
    OBJ_STRING,
    OBJ_PAIR,
    // OBJ_LIST,
	OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_NATIVE_FN,
    OBJ_LAST
};

// opcode definitions
enum { OP_HALT, OP_PUSH, OP_POP, OP_NUM, OP_STRING, OP_TRUE, OP_FALSE, OP_STORE, OP_DUP,
    OP_LOAD, OP_NATIVE, OP_RET, OP_CALL, OP_BRA, OP_BNT, OP_BNF, OP_NOP,
    OP_LIST
};

// opcode type
typedef intptr_t VMCode_t;

// code chunk structure
typedef struct CodeChunk {
    VMCode_t* code;
    size_t count;
    size_t capacity;
} CodeChunk;

// pair object
typedef struct Pair {
	Object base;    // base object
	Object* car;    // first element
	Object* cdr;    // second element
} Pair;

// string object
typedef struct String {
    Object base;        // base object
    const char value[]; // string value
} String;

// procedure object
typedef struct Procedure {
    Object base;        // base object
	int arity;          // number of arguments
	CodeChunk chunk;    // code chunk
    Object *name;      // function name
} Procedure;

// native function object
typedef struct NativeFunction {
    Object base;       // base object
    SKFunc func;       // pointer to native function
    int arity;         // number of arguments
    Object *name;      // function name
} NativeFunction;

// environment object
typedef struct Environment {
    Object base;            // base object
    //HashTable env;
} Environment;

// closure object
typedef struct Closure {
    Object base;            // base object
    Object* function;       // pointer to the function object
    // upvalues?
} Closure;

// Define the VM structure
typedef struct VM {
	int sp;                         // Stack pointer should be size_t or uint
    VMCode_t *ip;                   // Instruction pointer
    Object* head;                   // Head of the object list
    Object* stack[VM_STACK_SIZE];   // Stack of object pointers
    size_t numObjects;              // Number of allocated objects should be size_t
    size_t peakObjects;             // Peak number of allocated objects
	size_t peakAllocatedBytes;      // Peak allocated bytes
    size_t nextGC;                  // Threshold to trigger next GC
    size_t allocatedBytes;          // Total allocated bytes
    size_t gc_cycles;               // Number of GC cycles run

    //int vm_code_ptr;
    //VMCode_t vm_code[VM_MAX_CODE];

    HashTable *global_table;          // Hash table for global variables
	HashTable* interned_strings;      // Hash table for interned strings

    VMCode_t *return_stack[VM_STACK_SIZE]; // Return address stack
    int return_sp;                        // Return address stack pointer
    
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

// stack management
void vm_push(VM* pvm, Object* obj);
Object* vm_pop(VM* pvm);
Object* vm_peek(VM* pvm, int distance);
void vm_pop_n(VM* pvm, int n);

// object allocation
void *vm_alloc(VM* pvm, void *ptr, size_t old_size, size_t new_size);
Object* vm_newObject(VM* pvm, size_t size, int type);
Object* vm_newBoolean(VM* pvm, int val);
void vm_pushBoolean(VM* pvm, int val);
void vm_freeObjects(VM* pvm);
Object* vm_new_string(VM* pvm, const char* str);
Object* vm_new_proc(VM* pvm);
Object* vm_get_string(VM* pvm, const char* str);

// code generation and execution
int vm_emit(VM* pvm, VMCode_t opcode);
void vm_emit_number(VM* pvm, int val);
void vm_emit_string(VM* pvm, const char *str);
void vm_emit_object(VM* pvm, Object* obj);
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name);
CodeChunk* vm_get_current_chunk();
CodeChunk* vm_set_current_chunk(CodeChunk* chunk);
void vm_init_chunk(CodeChunk* chunk);
void vm_free_chunk(VM* pvm, CodeChunk* chunk);
VMCode_t* vm_get_chunk_ip();

void vm_exec(VM* pvm, VMCode_t *addr);
int vm_clear_stack(VM* pvm);
void vm_clear_code(VM* pvm);
int vm_reset(VM* pvm);
void vm_push_ip(VM* pvm, VMCode_t* ip);
VMCode_t* vm_pop_ip(VM* pvm);

// global variable management
void vm_define_global(VM* pvm, Object* key, Object* value);
void vm_update_global(VM *pvm, Object* key, Object *value);
Object* vm_get_global(VM *pvm, Object *key);

#endif // __VM_H
