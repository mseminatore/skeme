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

// uncomment to disallow redefining globals
// TODO - only redefines work at present! Must handle code gen into chunk that would get stored in var
//#define VM_NO_REDEFINE

// uncomment to enable VM debug logging
 //#define VM_DEBUG_LOG

#define VM_STACK_SIZE 1024
#define VM_FREE_LIST 128
#define VM_INITIAL_GC_THRESHOLD 256
#define VM_GC_HEAP_GROWTH_FACTOR 2

#define ARRAY_MIN_CAPACITY 8
#define ARRAY_GROWTH_FACTOR 2

#define GROW_CAPACITY(capacity) ((capacity) < ARRAY_MIN_CAPACITY ? ARRAY_MIN_CAPACITY : (capacity) * ARRAY_GROWTH_FACTOR)
#define GROW_ARRAY(type, array, oldCount, newCount) (type*)vm_realloc(&vm, array, sizeof(type) * (oldCount), sizeof(type) * (newCount))
//#define SHRINK_CAPACITY(capacity) ((capacity) >> 1)

#ifdef _WIN32
#   define UNUSED //__declspec(unused)
#else
#   define UNUSED __attribute__((unused))
#endif

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

#define VM_PRINT_FANCY 0
#define VM_PRINT_PLAIN 1
#define VM_PRINT_READABLE 1

#define PRINT_TRUE  "true"      // historically this would be #t
#define PRINT_FALSE "false"     // historically this would be #f
#define PRINT_NIL   "nil"       // historically '()

extern int g_bGCLog;
extern int g_bVMLog;
extern int g_bGCStress;

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

// object tagging definitions
#define TAG_BITS 2
#define TAG_BIT_SHIFT TAG_BITS
#define TAG_MASK ((1 << TAG_BITS) - 1)
#define TAG_NUM 0x1
#define TAG_OBJ 0x0
#define TAG_BOOL 0x2    // lowest bit set for booleans

// macros to check and convert to object pointers
#define IS_OBJ(obj)         ((obj) != NULL && !((intptr_t)(obj) & TAG_MASK))
#define AS_OBJ(ObjT, obj)   (IS_OBJ(obj) ? (ObjT*)(obj) : NULL)
#define AS_NUM(obj)         (IS_OBJ(obj) ? 0 : ((int)((intptr_t)(obj) >> TAG_BIT_SHIFT)))
#define AS_STRING(obj)      (IS_OBJ(obj) ? (String*)(obj) : NULL)

#define OBJ_TYPE(obj)       (IS_OBJ(obj) ? AS_OBJ(Object, obj)->type : OBJ_NULL)
#define SET_TYPE(obj, t)    do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->type = (t); } while(0)

#define MARK_OBJ(obj)       do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->marked = 1; } while(0)
#define UNMARK_OBJ(obj)     do { if (IS_OBJ(obj)) AS_OBJ(Object, obj)->marked = 0; } while(0)
#define IS_MARKED(obj)      (IS_OBJ(obj) ? AS_OBJ(Object, obj)->marked : 0)

// object classification
#define IS_NUM(obj)         (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_NUM))
#define IS_NIL(obj)         (obj == NULL)
#define IS_BOOL(obj)        (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_BOOL)))
#define IS_STRING(obj)      (OBJ_TYPE(obj) == OBJ_STRING)
#define IS_PAIR(obj)        (OBJ_TYPE(obj) == OBJ_PAIR)
#define IS_PROCEDURE(obj)   (OBJ_TYPE(obj) == OBJ_PROCEDURE)
#define IS_NATIVE_FN(obj)   (OBJ_TYPE(obj) == OBJ_NATIVE_FN)
#define IS_FUNCTION(obj)    (IS_PROCEDURE(obj) || IS_NATIVE_FN(obj))
#define IS_FLOAT(obj)       (OBJ_TYPE(obj) == OBJ_FLOAT)
#define IS_ENVIRONMENT(obj) (OBJ_TYPE(obj) == OBJ_ENVIRONMENT)
#define IS_CLOSURE(obj)     (OBJ_TYPE(obj) == OBJ_CLOSURE)
#define IS_TRUE(obj)        (((intptr_t)(obj)) == (TAG_BOOL | 0x1))
#define IS_FALSE(obj)       (((intptr_t)(obj)) == TAG_BOOL)
#define IS_PORT(obj)        (OBJ_TYPE(obj) == OBJ_PORT)

// macros to create tagged objects
#define MAKE_TRUE()         ((Object*)(TAG_BOOL | 0x1))
#define MAKE_FALSE()        ((Object*)TAG_BOOL)
#define MAKE_NUM(val)       ((Object*)((uintptr_t)((val) << TAG_BIT_SHIFT) | TAG_NUM))

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

// object types
enum 
{
    OBJ_MARK_BIT,
    OBJ_NULL,
    OBJ_STRING,
    OBJ_PAIR,
	OBJ_PROCEDURE,
    OBJ_NATIVE_FN,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_CHAR,
    OBJ_ENVIRONMENT,
    OBJ_FLOAT,
    OBJ_PORT,

	// mark the last object type
    OBJ_LAST
};

// opcode definitions
enum { OP_HALT, OP_PUSH, OP_POP, OP_NUM, OP_STRING, OP_TRUE, OP_FALSE, OP_STORE, OP_DUP,
    OP_LOAD, OP_RET, OP_CALL, OP_ICALL, OP_BRA, OP_BNT, OP_BNF, OP_NOP, OP_PROC, OP_CLOSURE,
	OP_LIST, OP_GET_LOCAL, OP_SET_LOCAL, OP_PRINT_TOS, OP_LAST
};

// opcode type
typedef intptr_t VMCode_t;

// code chunk structure
typedef struct CodeChunk {
    Object base;    // base object - not used presently!
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

// float object
typedef struct Float {
    Object base;    // base object
    float value;    // float value
} Float;

// procedure object
typedef struct Procedure {
    Object base;        // base object
	int arity;          // number of arguments
	CodeChunk chunk;    // code chunk
    String *name;      // function name
} Procedure;

// native function object
typedef struct NativeFunction {
    Object base;       // base object
    SKFunc func;       // pointer to native function
    int arity;         // number of arguments
    String *name;      // function name
} NativeFunction;

// environment object
typedef struct Environment {
    Object base;                // base object
    HashTable *bindings;             // hash table for variable bindings
    struct Environment* parent; // pointer to parent environment
} Environment;

// closure object
typedef struct Closure {
    Object base;            // base object
    Object* function;       // pointer to the function object
    // upvalues?
} Closure;

// port object
typedef struct File {
    Object base;            // base object
    FILE* fptr;             // file handle
    String* name;           // file name
} Port;

// Define the VM structure
typedef struct VM {
	int sp;                         // Stack pointer, should be size_t or uint?
    int fp;                         // Frame pointer
    VMCode_t *ip;                   // Instruction pointer
    Object* head;                   // Head of the object list
    Object* stack[VM_STACK_SIZE];   // Stack of object pointers
    size_t numObjects;              // Number of allocated objects should be size_t
    size_t peakObjects;             // Peak number of allocated objects
    size_t objectsCollected;        // Number of objects garbage collected
	size_t peakAllocatedBytes;      // Peak allocated bytes
    size_t nextGC;                  // Threshold to trigger next GC
    size_t allocatedBytes;          // Total allocated bytes
    size_t gc_cycles;               // Number of GC cycles run
	size_t max_stack_size;          // Maximum stack size reached
	size_t max_call_depth;          // Maximum call depth reached

    HashTable *global_table;        // Hash table for global variables
	HashTable* interned_strings;    // Hash table for interned strings

    Environment *current_scope;     // Current environment/scope

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

// basic functions
int vm_init(VM* pvm);
int vm_shutdown(VM* pvm);
void vm_gc(VM* pvm);
void vm_print_object(Object* obj, int flags, FILE *stream);

// stack management
void vm_push(VM* pvm, Object* obj);
Object* vm_pop(VM* pvm);
Object* vm_peek(VM* pvm, int distance);
void vm_pop_n(VM* pvm, int n);
Object* vm_fp_peek(VM* pvm, int distance);
int vm_clear_stack(VM* pvm);
void vm_push_ip(VM* pvm, VMCode_t* ip);
VMCode_t* vm_pop_ip(VM* pvm);
void vm_print_stack(VM* pvm);
String* vm_push_string(VM* pvm, const char* str);
void vm_push_global(VM* pvm, const char* name);

// object allocation
void *vm_alloc(VM* pvm, void *ptr, size_t old_size, size_t new_size);
Object* vm_newObject(VM* pvm, size_t size, int type);
Object* vm_newBoolean(VM* pvm, int val);
void vm_pushBoolean(VM* pvm, int val);
void vm_freeObjects(VM* pvm);
Object* vm_new_string(VM* pvm, const char* str);
Object* vm_new_proc(VM* pvm);
Object* vm_get_string(VM* pvm, const char* str);
Port* vm_new_port(VM* pvm, const char* name, FILE* fptr);
int vm_create_default_ports(VM* pvm);

// code generation
int vm_emit(VM* pvm, VMCode_t opcode);
void vm_emit_number(VM* pvm, int val);
void vm_emit_string(VM* pvm, const char *str);
void vm_emit_push_object(VM* pvm, Object* obj);
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name);
CodeChunk* vm_get_current_chunk();
CodeChunk* vm_set_current_chunk(CodeChunk* chunk);
void vm_init_chunk(CodeChunk* chunk);
void vm_free_chunk(VM* pvm, CodeChunk* chunk);
VMCode_t* vm_get_chunk_ip();

// execution
void vm_exec(VM* pvm, VMCode_t *addr);
void vm_clear_code(VM* pvm);
int vm_reset(VM* pvm);

// local/global variable management
int vm_define_global(VM* pvm, Object* key, Object* value);
int vm_update_global(VM *pvm, Object* key, Object *value);
Object* vm_get_global(VM *pvm, Object *key);
Object *vm_new_environment(VM* pvm);
void vm_free_environment(VM* pvm);
void vm_set_var(VM* pvm, const char* name, Object* value);
Object* vm_get_local_var(VM* pvm, const char* name);

#endif // __VM_H
