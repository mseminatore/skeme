//---------------------------------------------------------
// vm.h - definitions for the virtual machine for SK
//
// Copyright (c) 2025 Mark Seminatore
//----------------------------------------------------------

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

// Number of stack slots (allocated on the heap by vm_init). Each non-tail call
// uses its arguments plus 3 frame slots, so 64K slots allow roughly 10-15K
// nested calls. Override at build time, e.g.
//   make CFLAGS="-I. -I./hash/testy -O2 -DVM_STACK_SIZE=262144"
#ifndef VM_STACK_SIZE
#define VM_STACK_SIZE 65536
#endif

// How deeply natives may call back into Scheme (map, call/cc, apply from C).
// Each level nests C calls, so this guards the C stack: with the default 1MB
// stack, a Debug build crashed somewhere between 700 and 1500 levels and a
// Release build between 1500 and 2000.
#ifndef VM_MAX_NATIVE_DEPTH
#define VM_MAX_NATIVE_DEPTH 500
#endif
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

#define VM_PRINT_FANCY 0       // the REPL echo: as write, lists marked with '
#define VM_PRINT_PLAIN 1       // display: strings and characters as their text
#define VM_PRINT_READABLE 2    // write: strings quoted, characters as #\c, so it reads back

#define PRINT_TRUE  "true"      // historically this would be #t
#define PRINT_FALSE "false"     // historically this would be #f
#define PRINT_NIL   "()"        // empty list prints as ()

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
#define TAG_UNSPEC  0x6   // The unspecified value (result of display, newline, ...)
#define TAG_UNBOUND 0x7   // Placeholder for a global that is referenced but not yet defined

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
    OBJ_CONTINUATION,
    OBJ_ERROR,          // an error object: (error msg irritant ...), or an error the VM raised
    OBJ_VECTOR,         // #(a b c)
    OBJ_HASHTABLE,      // (make-hash-table)

	// mark the last object type
    OBJ_LAST
};

// Checks and conversions of tagged values. Each macro passes its argument to
// an inline function, so the argument is evaluated exactly once:
// AS_NUM(vm_pop(pvm)) pops one value. The (Object*) casts let call sites
// pass anything pointer-sized, as the macros always have.
static inline int sk_is_obj(const Object* obj) {
    return obj != NULL && !((intptr_t)obj & TAG_MASK);
}
static inline int sk_tag_is(const Object* obj, int tag) {
    return sk_is_obj(obj) ? 0 : (((intptr_t)obj & TAG_MASK) == tag);
}
static inline Object* sk_as_obj(Object* obj) {
    return sk_is_obj(obj) ? obj : NULL;
}
static inline int sk_as_num(const Object* obj) {
    return sk_is_obj(obj) ? 0 : (int)((intptr_t)obj >> TAG_BIT_SHIFT);
}
static inline int sk_obj_type(const Object* obj) {
    return sk_is_obj(obj) ? obj->type : OBJ_NULL;
}
static inline void sk_set_mark(Object* obj, uint8_t marked) {
    if (sk_is_obj(obj)) obj->marked = marked;
}

#define IS_OBJ(obj)         sk_is_obj((const Object*)(obj))
#define AS_OBJ(ObjT, obj)   ((ObjT*)sk_as_obj((Object*)(obj)))
#define AS_NUM(obj)         sk_as_num((const Object*)(obj))
#define AS_STRING(obj)      ((String*)sk_as_obj((Object*)(obj)))

#define OBJ_TYPE(obj)       sk_obj_type((const Object*)(obj))
#define SET_TYPE(obj, t)    do { Object* o_ = (Object*)(obj); if (IS_OBJ(o_)) o_->type = (t); } while(0)

#define MARK_OBJ(obj)       sk_set_mark((Object*)(obj), 1)
#define UNMARK_OBJ(obj)     sk_set_mark((Object*)(obj), 0)
#define IS_MARKED(obj)      sk_is_marked((const Object*)(obj))
static inline int sk_is_marked(const Object* obj) {
    return sk_is_obj(obj) ? obj->marked : 0;
}

// object classification - integers
#define IS_NUM(obj)         sk_tag_is((const Object*)(obj), TAG_NUM)
#define IS_NIL(obj)         ((obj) == NULL)

// object classification - booleans (false=0x02, true=0x0A)
#define IS_BOOL(obj)        sk_tag_is((const Object*)(obj), TAG_BOOL)
#define IS_TRUE(obj)        (((intptr_t)(obj)) == (TAG_BOOL | 0x8))
#define IS_FALSE(obj)       (((intptr_t)(obj)) == TAG_BOOL)

// the unspecified value: what an expression with no useful result yields.
// The REPL does not echo it.
#define MAKE_UNSPEC()       ((Object*)TAG_UNSPEC)
#define IS_UNSPEC(obj)      (((intptr_t)(obj)) == TAG_UNSPEC)

// the value of a global the compiler has seen referenced but that has not
// been defined yet; reading it is an "unbound variable" error
#define MAKE_UNBOUND()      ((Object*)TAG_UNBOUND)
#define IS_UNBOUND(obj)     (((intptr_t)(obj)) == TAG_UNBOUND)

// object classification - characters
#define IS_CHAR(obj)        sk_tag_is((const Object*)(obj), TAG_CHAR)
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
static inline int sk_is_float(const Object* obj) {
    return sk_tag_is(obj, TAG_FLOAT) || sk_obj_type(obj) == OBJ_FLOAT;
}
#define IS_TAGGED_FLOAT(obj)    sk_tag_is((const Object*)(obj), TAG_FLOAT)
#define AS_TAGGED_FLOAT(obj)    (sk_decode_float((intptr_t)(obj) >> TAG_BIT_SHIFT))
#define MAKE_TAGGED_FLOAT(f)    ((Object*)(((uintptr_t)sk_encode_float(f) << TAG_BIT_SHIFT) | TAG_FLOAT))
#else
static inline int sk_is_float(const Object* obj) {
    return sk_obj_type(obj) == OBJ_FLOAT;
}
#define IS_TAGGED_FLOAT(obj)    (0)
#endif
#define IS_FLOAT(obj)           sk_is_float((const Object*)(obj))

// Return addresses (stored on unified stack)
#define IS_RETADDR(obj)         sk_tag_is((const Object*)(obj), TAG_RETADDR)
#define AS_RETADDR(obj)         ((VMCode_t*)((intptr_t)(obj) >> TAG_BIT_SHIFT))
#define MAKE_RETADDR(addr)      ((Object*)(((uintptr_t)(addr) << TAG_BIT_SHIFT) | TAG_RETADDR))

// object classification - heap types
#define IS_STRING(obj)      (OBJ_TYPE(obj) == OBJ_STRING)
#define IS_SYMBOL(obj)      (OBJ_TYPE(obj) == OBJ_SYMBOL)
#define IS_PAIR(obj)        (OBJ_TYPE(obj) == OBJ_PAIR)
#define IS_PROCEDURE(obj)   (OBJ_TYPE(obj) == OBJ_PROCEDURE)
#define IS_NATIVE_FN(obj)   (OBJ_TYPE(obj) == OBJ_NATIVE_FN)
#define IS_FUNCTION(obj)    sk_is_function((const Object*)(obj))
#define IS_ENVIRONMENT(obj) (OBJ_TYPE(obj) == OBJ_ENVIRONMENT)
#define IS_CLOSURE(obj)     (OBJ_TYPE(obj) == OBJ_CLOSURE)
#define IS_PORT(obj)        (OBJ_TYPE(obj) == OBJ_PORT)
#define IS_CONTINUATION(obj) (OBJ_TYPE(obj) == OBJ_CONTINUATION)
static inline int sk_is_function(const Object* obj) {
    int type = sk_obj_type(obj);
    return type == OBJ_PROCEDURE || type == OBJ_NATIVE_FN;
}

// Numeric classification (integer or float)
#define IS_NUMERIC(obj)     sk_is_numeric((const Object*)(obj))
static inline int sk_is_numeric(const Object* obj) {
    return sk_tag_is(obj, TAG_NUM) || sk_is_float(obj);
}

// Convert any numeric to double (for mixed arithmetic)
#define AS_DOUBLE(obj)      sk_as_double((const Object*)(obj))
#if SK_64BIT
static inline double sk_as_double(const Object* obj) {
    return sk_is_float(obj) ? (double)AS_TAGGED_FLOAT(obj) : (double)sk_as_num(obj);
}
#else
static inline double sk_as_double(const Object* obj);   // defined after Float
#endif

// macros to create tagged objects
#define MAKE_TRUE()         ((Object*)(TAG_BOOL | 0x8))
#define MAKE_FALSE()        ((Object*)TAG_BOOL)
#define MAKE_NUM(val)       ((Object*)((uintptr_t)((val) << TAG_BIT_SHIFT) | TAG_NUM))

typedef struct SKState SKState;
typedef int (*SKFunc)(SKState* pSK);

void yyerror(SKState* pSK, const char *s);

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
    OP_TAILCALL_X,  // tail call with fn under args (tail form of OP_XCALL)
    OP_LOAD_GLOBAL, // push global: operands key, cached value, cache version (see op_load_global)
    OP_DISCARD_TOS, // discard a top-level expr result without printing it (script input)
    OP_CAPTURE_LOCAL,   // push the (shared) upvalue for local slot idx, for OP_MAKE_CLOSURE
    OP_CAPTURE_UPVALUE, // push the current closure's upvalue idx, for OP_MAKE_CLOSURE
    OP_LIST_TAIL,   // like OP_LIST n, but the list ends in the value on top (a dotted list)
    OP_LAST         // last opcode
};
    
// opcode type
typedef intptr_t VMCode_t;

// code chunk structure
// Where code came from: one entry where the source location changes, for
// the code words from 'offset' up to the next entry's (see vm_emit)
typedef struct SourceLine {
    size_t offset;      // first code word at this location
    int file;           // index into VM.source_files
    int line;
} SourceLine;

typedef struct CodeChunk {
    Object base;    // base object - not used presently!
    VMCode_t* code;
    size_t count;
    size_t capacity;
    SourceLine* lines;  // source locations, by offset (runtime error messages)
    size_t line_count;
    size_t line_capacity;
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

#if !SK_64BIT
static inline double sk_as_double(const Object* obj) {
    return sk_is_float(obj) ? (double)((const Float*)obj)->value : (double)sk_as_num(obj);
}
#endif

// Upvalue descriptor - describes how to capture a variable
typedef struct {
    int is_local;       // 1 if capturing from immediate enclosing scope, 0 if from closure
    int index;          // local slot index or upvalue index in enclosing closure
} UpvalueDesc;

// procedure object
typedef struct Procedure {
    Object base;        // base object
	int arity;          // number of parameters (a rest parameter counts as one)
    int has_rest;       // 1 if the last parameter receives the remaining arguments as a list
	CodeChunk chunk;    // code chunk
    String *name;       // function name
    UpvalueDesc* upvalues;  // array of upvalue descriptors (NULL if no upvalues)
    int upvalue_count;      // number of upvalues
    uint64_t call_count;    // profiling: number of times called
} Procedure;

// native arities other than a fixed count; the VM checks each call
#define SK_ARITY_ANY -1
#define SK_ARITY_ZERO_OR_ONE -2
#define SK_ARITY_ONE_OR_MORE -3
#define SK_ARITY_TWO_OR_MORE -4

// native function object
typedef struct NativeFunction {
    Object base;       // base object
    SKFunc func;       // pointer to native function
    int arity;         // number of arguments, or an SK_ARITY_ value
    String *name;      // function name
} NativeFunction;

// environment object
typedef struct Environment {
    Object base;                // base object
    HashTable *bindings;             // hash table for variable bindings
    struct Environment* parent; // pointer to parent environment
} Environment;

// closure object
// A captured variable, shared by every closure that captures it. While the
// frame that owns the variable is live the upvalue is "open" and reads and
// writes the variable's stack slot, so the frame and the closures see each
// other's set!s. When the frame goes away the upvalue is "closed": the value
// moves into the upvalue, where the closures keep sharing it.
typedef struct Upvalue {
    Object base;                // base object
    int slot;                   // stack index while open, -1 once closed
    Object* closed;             // the value, once closed
    struct Upvalue* next_open;  // VM's open upvalue list, highest slot first
} Upvalue;

// An escape continuation, made by call/cc. Calling it returns its value
// from that call/cc, but only while the call/cc is still running (its
// "dynamic extent"); afterwards it is invalid. See sk_call_cc.
// a hash table (SRFI-69 style): a table of the hash library, with Scheme
// objects as keys and values (the GC traces them). The empty list, which
// is NULL, is stored as the unbound tag, since the library marks an empty
// slot with a NULL key.
enum { HASH_EQUAL, HASH_EQV };      // HashTableObject.kind: how keys compare
typedef struct HashTableObject {
    Object base;
    HashTable* table;
    int kind;
} HashTableObject;

// a vector: 'length' elements, stored in the same allocation, after the
// header (vm_new_vector)
typedef struct Vector {
    Object base;
    int length;
    Object** items;         // points just past this header
} Vector;

// an error object (R7RS): what (error message irritant ...) raises, and
// what a runtime error raises when a handler is installed
typedef struct ErrorObject {
    Object base;
    Object* message;        // usually a string
    Object* irritants;      // a list
} ErrorObject;

typedef struct Continuation {
    Object base;
    int valid;              // 1 while its call/cc has not returned
} Continuation;

typedef struct Closure {
    Object base;            // base object
    struct Procedure* function;  // pointer to the procedure
    Upvalue** upvalues;     // array of captured variables
    int upvalue_count;      // number of captured variables
} Closure;

// port object
typedef struct File {
    Object base;            // base object
    FILE* fptr;             // file handle
    String* name;           // file name
} Port;

// forward declaration of VM structure for use in type descriptors
struct VM;

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
    Object** stack;                 // Unified stack (data + return addresses), VM_STACK_SIZE slots
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
    HashTable* interned_symbols;    // Hash table for interned symbols (one object per name)

    Environment *current_scope;     // Current environment/scope
    Closure* current_closure;       // Currently executing closure (for upvalue access)
    int abort_exec;                 // set by a fatal runtime error (stack overflow); unwinds to top level
    int exec_depth;                 // nesting of vm_exec / vm_call_function
    size_t globals_version;         // bumped on every global write; invalidates OP_LOAD_GLOBAL caches
    struct Upvalue* open_upvalues;  // upvalues still pointing into the stack, highest slot first
    Object* pending_call;           // procedure a native asked the VM to call (vm_request_call)
    int pending_nargs;              // its argument count; the arguments are on the stack
    Object* call_root;              // callee kept alive while a call allocates (rest lists)
    Object* escape_to;              // continuation being escaped to (see vm_escape), or NULL
    Object* escape_value;           // the value it returns

    // Exception handlers (R7RS raise, guard, with-exception-handler): a list,
    // innermost first, of handler procedures and, for guard, escape
    // continuations. vm_raise calls or escapes to the first. 'uncatchable'
    // marks an error a handler must not see (stack overflow, heap limit).
    Object* handlers;
    int uncatchable;
    Object* guard_unhandled;        // what a guard's clauses return when none matched

    // GC roots outside the object graph. The GC traces the objects that
    // bytecode refers to (see markChunk), starting from these chunks: the
    // top-level code being compiled or run (the REPL's, each eval's). A
    // procedure the compiler is still building is a compile root until its
    // parent's code refers to it.
    // Running out of memory. With a heap limit (bytes, 0 for none), a run
    // whose live objects outgrow it stops with an "out of memory" error and
    // the session goes on. If memory really runs out, the VM cannot go on:
    // vm_fatal calls fatal_handler (sk_set_fatal_handler), which may longjmp
    // out, or else prints the message and exits.
    size_t heap_limit;
    void (*fatal_handler)(struct SKState* sk, const char* msg);

    char** source_files;            // file names that code comes from (SourceLine.file)
    int source_file_count;
    int source_file_capacity;

    CodeChunk** root_chunks;
    int root_chunk_count;
    int root_chunk_capacity;
    Object** compile_roots;
    int compile_root_count;
    int compile_root_capacity;

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
void vm_format_float(double value, char* buf, size_t size);

// stack management - inlined for performance
// Out-of-range accesses are runtime errors in every build: they report the
// error, set abort_exec so execution unwinds to the top level, and touch
// nothing outside the stack.
void vm_abort(VM* pvm, const char* msg);
void vm_stop(VM* pvm);     // like vm_abort, for an error already reported
void vm_abort_uncatchable(VM* pvm, const char* msg);   // vm_abort that handlers do not see
Object* vm_raise(VM* pvm, Object* obj, int continuable);  // raise obj (see VM.handlers)
Object* vm_new_error(VM* pvm, Object* message, Object* irritants);
Object* vm_new_vector(VM* pvm, int length, Object* fill);
#define IS_VECTOR(obj)      (OBJ_TYPE(obj) == OBJ_VECTOR)
#define IS_HASHTABLE(obj)   (OBJ_TYPE(obj) == OBJ_HASHTABLE)
void vm_format_object(Object* obj, int readable, char* buf, size_t size);   // printed form, truncated
void vm_fatal(VM* pvm, const char* msg);   // an unrecoverable failure (see VM.fatal_handler)
void* vm_check_alloc(VM* pvm, void* p);     // p, or vm_fatal if an allocation returned NULL

void vm_stack_overflow(VM* pvm);

static inline void vm_push(VM* pvm, Object* obj) {
    // the common case first; the rare overflow is out of line (pushes are hot)
    if (pvm->sp < VM_STACK_SIZE) {
        pvm->stack[pvm->sp++] = obj;
        if (pvm->sp > (int)pvm->max_stack_size)
            pvm->max_stack_size = pvm->sp;
    } else
        vm_stack_overflow(pvm);
}
static inline Object* vm_pop(VM* pvm) {
    if (pvm->sp <= 0) { vm_abort(pvm, "stack underflow"); return MAKE_UNSPEC(); }
    return pvm->stack[--pvm->sp];
}
static inline Object* vm_peek(VM* pvm, int distance) {
    if (pvm->sp - 1 - distance < 0) { vm_abort(pvm, "stack underflow"); return MAKE_UNSPEC(); }
    return pvm->stack[pvm->sp - 1 - distance];
}
static inline Object* vm_fp_peek(VM* pvm, int distance) {
    if (pvm->fp - 1 - distance < 0) { vm_abort(pvm, "stack underflow"); return MAKE_UNSPEC(); }
    return pvm->stack[pvm->fp - 1 - distance];
}
static inline void vm_fp_set(VM* pvm, int distance, Object* value) {
    if (pvm->fp - 1 - distance < 0) { vm_abort(pvm, "stack underflow"); return; }
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
void vm_push_root_chunk(VM* pvm, CodeChunk* chunk);
void vm_pop_root_chunk(VM* pvm);
void vm_push_compile_root(VM* pvm, Object* obj);
void vm_pop_compile_root(VM* pvm);
void vm_chunk_truncate(CodeChunk* chunk, size_t count);
int vm_current_location(VM* pvm, const char** file, int* line);
void vm_free_chunk(VM* pvm, CodeChunk* chunk);
VMCode_t* vm_get_chunk_ip();

// the lexer's position, for the code being compiled (lexer.c); 0 if none
int sk_source_location(struct SKState* pSK, const char** file, int* line);

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
void vm_request_call(VM* pvm, Object* fn, int nargs);
void vm_build_list(VM* pvm, int n, Object* tail);   // cons the top n stack values onto tail
void vm_escape(VM* pvm, Object* k, Object* value);   // call an escape continuation
int vm_has_global(VM *pvm, Object *key);
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
