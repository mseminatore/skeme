#ifndef __VM_H
#define __VM_H

#include <stdio.h>
#include <stdint.h> // Add this include at the top of the file, before typedefs
#include <assert.h>
#include "hash/hash.h"
#include "platform.h"

#define VM_OK 1
#define VM_ERR -1

// Result codes for vm_exec_one()
typedef enum {
    VM_CONTINUE,    // Keep executing next instruction
    VM_HALT,        // OP_HALT encountered, stop execution
    VM_ERROR        // Error occurred
} VMExecResult;

// configuration
// #define VM_USE_FREE_LIST

// uncomment to enable GC stress testing (collect on every allocation)
// #define VM_STRESS_GC

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

#ifndef TRUE
#   define TRUE 1
#endif

#ifndef FALSE
#   define FALSE 0
#endif

#ifdef _DEBUG
#   define CHECK_THAT(cond)            do { assert(cond); if (!(cond)) return VM_ERR; } while(0)
#   define CHECK_THAT_MSG(pSK, cond, msg)   do { if (!(cond)) { yyerror(pSK, msg); return VM_ERR; } } while(0)
#else
#   define CHECK_THAT(cond)            do { if (!(cond)) return VM_ERR; } while(0)
#   define CHECK_THAT_MSG(pSK, cond, msg)   do { if (!(cond)) { yyerror(pSK, msg); return VM_ERR; } } while(0)
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
extern int g_bProfile;


//-----------------------------
// Define the Object structure
//-----------------------------
typedef struct Object {
    uint8_t type;           // Type of the object (e.g., integer, string, etc.)
    uint8_t marked;         // Mark flag for garbage collection
    size_t size;            // Size of the object (for strings, pairs, etc.)
    struct Object* next;    // Pointer to the next object in the linked list
} Object;

// 64-bit platform detection
#if INTPTR_MAX == INT64_MAX
#define SK_64BIT 1
#else
#define SK_64BIT 0
#endif

// object tagging definitions (3-bit scheme)
// Tag values: 0=obj, 1=int, 2=bool, 3=char, 4=float (64-bit only), 5-7=reserved
#define TAG_BITS 3
#define TAG_BIT_SHIFT TAG_BITS
#define TAG_MASK ((1 << TAG_BITS) - 1)  // 0x7
#define TAG_OBJ     0x0   // Heap objects (8-byte aligned pointers)
#define TAG_NUM     0x1   // Integers: value << 3 | 0x1
#define TAG_BOOL    0x2   // Booleans: false=0x02, true=0x0A
#define TAG_CHAR    0x3   // Characters: char_value << 3 | 0x3
#define TAG_FLOAT   0x4   // Floats (64-bit only): float_bits << 3 | 0x4
#define TAG_RETADDR 0x5   // Return addresses (code pointers)

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

// object classification - integers
#define IS_NUM(obj)         (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_NUM))
#define IS_NIL(obj)         (obj == NULL)

// object classification - booleans (false=0x02, true=0x0A)
#define IS_BOOL(obj)        (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_BOOL))
#define IS_TRUE(obj)        (((intptr_t)(obj)) == (TAG_BOOL | 0x8))
#define IS_FALSE(obj)       (((intptr_t)(obj)) == TAG_BOOL)

// object classification - characters
#define IS_CHAR(obj)        (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_CHAR))
#define AS_CHAR(obj)        ((char)((intptr_t)(obj) >> TAG_BIT_SHIFT))
#define MAKE_CHAR(c)        ((Object*)((uintptr_t)((unsigned char)(c) << TAG_BIT_SHIFT) | TAG_CHAR))

// object classification - floats (tagged on 64-bit, heap on 32-bit)
#if SK_64BIT
static inline uint32_t sk_encode_float(float f) { 
    union { float f; uint32_t u; } conv; conv.f = f; return conv.u; 
}
static inline float sk_decode_float(intptr_t bits) { 
    union { float f; uint32_t u; } conv; conv.u = (uint32_t)bits; return conv.f; 
}
#define IS_TAGGED_FLOAT(obj)    (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_FLOAT))
#define AS_TAGGED_FLOAT(obj)    (sk_decode_float((intptr_t)(obj) >> TAG_BIT_SHIFT))
#define MAKE_TAGGED_FLOAT(f)    ((Object*)(((uintptr_t)sk_encode_float(f) << TAG_BIT_SHIFT) | TAG_FLOAT))
#define IS_FLOAT(obj)           (IS_TAGGED_FLOAT(obj) || (OBJ_TYPE(obj) == OBJ_FLOAT))
#else
#define IS_TAGGED_FLOAT(obj)    (0)
#define IS_FLOAT(obj)           (OBJ_TYPE(obj) == OBJ_FLOAT)
#endif

// Return addresses (stored on unified stack)
#define IS_RETADDR(obj)         (!IS_OBJ(obj) && (((intptr_t)(obj) & TAG_MASK) == TAG_RETADDR))
#define AS_RETADDR(obj)         ((VMCode_t*)((intptr_t)(obj) >> TAG_BIT_SHIFT))
#define MAKE_RETADDR(addr)      ((Object*)(((uintptr_t)(addr) << TAG_BIT_SHIFT) | TAG_RETADDR))

// object classification - heap types
#define IS_STRING(obj)      (OBJ_TYPE(obj) == OBJ_STRING)
#define IS_SYMBOL(obj)      (OBJ_TYPE(obj) == OBJ_SYMBOL)
#define IS_PAIR(obj)        (OBJ_TYPE(obj) == OBJ_PAIR)
#define IS_PROCEDURE(obj)   (OBJ_TYPE(obj) == OBJ_PROCEDURE)
#define IS_NATIVE_FN(obj)   (OBJ_TYPE(obj) == OBJ_NATIVE_FN)
#define IS_FUNCTION(obj)    (IS_PROCEDURE(obj) || IS_NATIVE_FN(obj))
#define IS_ENVIRONMENT(obj) (OBJ_TYPE(obj) == OBJ_ENVIRONMENT)
#define IS_CLOSURE(obj)     (OBJ_TYPE(obj) == OBJ_CLOSURE)
#define IS_PORT(obj)        (OBJ_TYPE(obj) == OBJ_PORT)

// Numeric classification (integer or float)
#define IS_NUMERIC(obj)     (IS_NUM(obj) || IS_FLOAT(obj))

// Convert any numeric to double (for mixed arithmetic)
#if SK_64BIT
#define AS_DOUBLE(obj)      (IS_FLOAT(obj) ? (double)AS_TAGGED_FLOAT(obj) : (double)AS_NUM(obj))
#else
#define AS_DOUBLE(obj)      (IS_FLOAT(obj) ? (double)((Float*)(obj))->value : (double)AS_NUM(obj))
#endif

// macros to create tagged objects
#define MAKE_TRUE()         ((Object*)(TAG_BOOL | 0x8))
#define MAKE_FALSE()        ((Object*)TAG_BOOL)
#define MAKE_NUM(val)       ((Object*)((uintptr_t)((val) << TAG_BIT_SHIFT) | TAG_NUM))

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

void yyerror(SKState* pSK, const char *s);

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
    OBJ_SYMBOL,

	// mark the last object type
    OBJ_LAST
};

// opcode definitions
enum { 
    OP_HALT,        // halt the VM
    OP_PUSH,        // push a value onto the stack
    OP_POP,         // pop a value from the stack
    OP_PUSH_IP,     // push instruction pointer onto return stack
    OP_POP_IP,      // pop instruction pointer from return stack
    OP_PUSH_FP,     // push frame pointer onto stack
    OP_POP_FP,      // pop frame pointer from stack
    OP_PUSH_SP,     // push stack pointer onto stack
    OP_POP_SP,      // pop stack pointer from stack    
    OP_NUM,         // push a numeric value onto the stack
    OP_STRING,      // push a string value onto the stack
    OP_TRUE,        // push the boolean value true onto the stack
    OP_FALSE,       // push the boolean value false onto the stack
    OP_STORE,       // store a value in a variable
    OP_DUP,         // duplicate the top value on the stack
    OP_LOAD,        // load a value from a variable
    OP_RET,         // return from a function
    OP_CALL,        // call a function
    OP_ICALL,       // call an indirect function
    OP_XCALL,       // call with fn under args: stack [fn, arg1..argN]
    OP_TAILCALL,    // tail call (reuses current frame) - function in bytecode
    OP_TAILCALL_I,  // indirect tail call - function on stack
    OP_BRA,         // branch always
    OP_BNT,         // branch if not true
    OP_BNF,         // branch if not false
    OP_NOP,         // no operation
    OP_MAKE_CLOSURE, // create closure from procedure + upvalues
    OP_GET_UPVALUE, // get captured variable by index
    OP_SET_UPVALUE, // set captured variable by index
	OP_LIST,        // list construction
    OP_GET_LOCAL,   // get a local variable
    OP_SET_LOCAL,   // set a local variable
    OP_PRINT_TOS,   // print the top of the stack
    // specialized arithmetic opcodes
    OP_ADD,         // (+ a b) inline
    OP_SUB,         // (- a b) inline
    OP_MUL,         // (* a b) inline
    OP_LT,          // (< a b) inline
    OP_LE,          // (<= a b) inline
    OP_EQ_NUM,      // (= a b) inline numeric equality
    // small-constant opcodes
    OP_NUM_0,       // push 0
    OP_NUM_1,       // push 1
    OP_NUM_2,       // push 2
    OP_SYMBOL,      // push a symbol value onto the stack
    OP_LAST         // last opcode
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

// Upvalue descriptor - describes how to capture a variable
typedef struct {
    int is_local;       // 1 if capturing from immediate enclosing scope, 0 if from closure
    int index;          // local slot index or upvalue index in enclosing closure
} UpvalueDesc;

// procedure object
typedef struct Procedure {
    Object base;        // base object
	int arity;          // number of arguments
	CodeChunk chunk;    // code chunk
    String *name;       // function name
    UpvalueDesc* upvalues;  // array of upvalue descriptors (NULL if no upvalues)
    int upvalue_count;      // number of upvalues
    uint64_t call_count;    // profiling: number of times called
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
    struct Procedure* function;  // pointer to the procedure
    Object** upvalues;      // array of captured values
    int upvalue_count;      // number of captured values
} Closure;

// port object
typedef struct File {
    Object base;            // base object
    FILE* fptr;             // file handle
    String* name;           // file name
} Port;

// Type descriptor for extensible object types
typedef struct SKTypeDesc {
    const char* name;                           // type name for display, e.g. "fraction"
    void (*mark_fn)(struct VM* pvm, Object* obj);   // GC mark: trace any Object* fields (NULL = no-op)
    void (*free_fn)(struct VM* pvm, Object* obj);   // cleanup: close handles, free sub-allocs (NULL = no-op)
    void (*print_fn)(Object* obj, int mode, FILE* stream); // display/write (NULL = #<typename>)
    int (*equal_fn)(Object* a, Object* b);      // eqv?/equal? (NULL = pointer equality)
} SKTypeDesc;

// Define the VM structure
typedef struct VM {
	int sp;                         // Stack pointer, should be size_t or uint?
    int fp;                         // Frame pointer
    VMCode_t *ip;                   // Instruction pointer
    Object* head;                   // Head of the object list
    Object* stack[VM_STACK_SIZE];   // Unified stack (data + return addresses)
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
    Closure* current_closure;       // Currently executing closure (for upvalue access)
    
    int grayCount;
    int grayCapacity;
    Object** grayStack;

    struct SKState* sk_state;       // Back-pointer to SKState

    // Extensible type registry
    SKTypeDesc* custom_types;       // Array of registered type descriptors
    int num_custom_types;           // Number of registered custom types
    int custom_types_capacity;      // Capacity of custom_types array

    // Profiling counters (active when g_bProfile is set)
    uint64_t opcode_counts[OP_LAST]; // per-opcode execution counts
    uint64_t total_opcodes;          // total instructions executed
    double gc_time_ms;               // cumulative GC time in milliseconds
    double native_call_time_ms;      // cumulative native function call time
    double exec_time_ms;             // total vm_exec time

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

// stack management - inlined for performance
// Debug builds get bounds checks via assert(); release (NDEBUG) gets zero overhead
static inline void vm_push(VM* pvm, Object* obj) {
    assert(pvm->sp < VM_STACK_SIZE && "Stack overflow");
    pvm->stack[pvm->sp++] = obj;
    if (pvm->sp > (int)pvm->max_stack_size)
        pvm->max_stack_size = pvm->sp;
}
static inline Object* vm_pop(VM* pvm) {
    assert(pvm->sp > 0 && "Stack underflow");
    return pvm->stack[--pvm->sp];
}
static inline Object* vm_peek(VM* pvm, int distance) {
    assert(pvm->sp - 1 - distance >= 0 && "Stack underflow on peek");
    return pvm->stack[pvm->sp - 1 - distance];
}
static inline Object* vm_fp_peek(VM* pvm, int distance) {
    assert(pvm->fp - 1 - distance >= 0 && "Stack underflow on fp_peek");
    return pvm->stack[pvm->fp - 1 - distance];
}
static inline void vm_fp_set(VM* pvm, int distance, Object* value) {
    assert(pvm->fp - 1 - distance >= 0 && "Stack underflow on fp_set");
    pvm->stack[pvm->fp - 1 - distance] = value;
}
void vm_stack_replace_n(VM* pvm, int distance, Object* obj);
void vm_pop_n(VM* pvm, int n);
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
Object* vm_new_symbol(VM* pvm, const char* name);
Object* vm_new_proc(VM* pvm);
Object* vm_new_float(VM* pvm, float value);
float vm_get_float(Object* obj);
Object* vm_get_string(VM* pvm, const char* str);
Port* vm_new_port(VM* pvm, const char* name, FILE* fptr);
int vm_create_default_ports(VM* pvm);

// code generation
int vm_emit(VM* pvm, VMCode_t opcode);
void vm_emit_number(VM* pvm, int val);
void vm_emit_string(VM* pvm, const char *str);
void vm_emit_symbol(VM* pvm, const char *name);
void vm_emit_push_object(VM* pvm, Object* obj);
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name);
CodeChunk* vm_get_current_chunk();
CodeChunk* vm_set_current_chunk(VM* pvm, CodeChunk* chunk);
void vm_init_chunk(CodeChunk* chunk);
void vm_free_chunk(VM* pvm, CodeChunk* chunk);
VMCode_t* vm_get_chunk_ip();

// execution
void vm_exec(VM* pvm, VMCode_t *addr);
void vm_clear_code(VM* pvm);
int vm_reset(VM* pvm);

// profiling
void vm_print_profile(VM* pvm);

// local/global variable management
int vm_define_global(VM* pvm, Object* key, Object* value);
int vm_update_global(VM *pvm, Object* key, Object *value);
Object* vm_get_global(VM *pvm, Object *key);
Object *vm_new_environment(VM* pvm);
void vm_free_environment(VM* pvm);
void vm_set_var(VM* pvm, const char* name, Object* value);
Object* vm_get_local_var(VM* pvm, const char* name);

// Extended local var lookup that also returns scope depth (0=current, 1=parent, etc.)
// scope_depth of -1 means not found in any local scope (global)
Object* vm_get_local_var_ex(VM* pvm, const char* name, int* scope_depth);

// Call a function from C code with given arguments
// Works with both native functions and Scheme procedures
Object* vm_call_function(VM* pvm, Object* fn, int nargs, Object** args);

// Extensible type registration
// Returns a type ID >= OBJ_LAST, or -1 on failure
int vm_register_type(VM* pvm, const SKTypeDesc* desc);

// Look up type descriptor for a custom type (returns NULL if not found)
const SKTypeDesc* vm_get_type_desc(VM* pvm, int type_id);

#endif // __VM_H
