# Extending Skeme with Custom Types

Skeme supports registering custom object types from C code. This allows extension
modules to add new data types (e.g. fractions, vectors, sockets) without modifying
the core interpreter.

## Overview

Custom types integrate with:

- **Garbage collection** — the GC will call your mark and free callbacks
- **Printing** — `display`, `write`, and the REPL use your print callback
- **Type introspection** — `typeof` returns your type's name

## API Reference

### SKTypeDesc

A type descriptor that defines the behavior of a custom type:

```c
typedef struct SKTypeDesc {
    const char* name;                              // type name, e.g. "fraction"
    void (*mark_fn)(VM* pvm, Object* obj);         // GC: trace Object* fields
    void (*free_fn)(VM* pvm, Object* obj);         // cleanup on deallocation
    void (*print_fn)(Object* obj, int mode, FILE* stream); // display/write
    int (*equal_fn)(Object* a, Object* b);         // eqv?/equal? support
} SKTypeDesc;
```

All callbacks are optional — set to `NULL` for default behavior:

| Callback   | If NULL                        |
|------------|--------------------------------|
| `mark_fn`  | No references traced (leaf)    |
| `free_fn`  | No cleanup needed              |
| `print_fn` | Prints `#<typename>`           |
| `equal_fn` | Pointer equality               |

### sk_register_type

```c
int sk_register_type(SKState* pSK, const SKTypeDesc* desc);
```

Registers a new type and returns its type ID (an integer `>= OBJ_LAST`).
Returns `-1` on failure.

The type ID is used when allocating instances with `vm_newObject()`.

### vm_newObject

```c
Object* vm_newObject(VM* pvm, size_t size, int type);
```

Allocates a new object of the given size and type. The object is automatically
added to the GC's linked list.

### vm_get_type_desc

```c
const SKTypeDesc* vm_get_type_desc(VM* pvm, int type_id);
```

Returns the descriptor for a registered custom type, or `NULL` if the type ID
is not a registered custom type.

## Writing a Custom Type

### Step 1: Define your struct

Your struct **must** have `Object base` as the first field:

```c
typedef struct {
    Object base;       // required — must be first
    int numerator;
    int denominator;
} Fraction;
```

### Step 2: Write callbacks

Only implement the callbacks you need:

```c
// Print: called by display, write, and the REPL
static void fraction_print(Object* obj, int mode, FILE* stream) {
    Fraction* f = (Fraction*)obj;
    fprintf(stream, "%d/%d", f->numerator, f->denominator);
}

// Equal: called by eqv? and equal?
static int fraction_equal(Object* a, Object* b) {
    Fraction* fa = (Fraction*)a;
    Fraction* fb = (Fraction*)b;
    return fa->numerator * fb->denominator == fb->numerator * fa->denominator;
}
```

**When to implement `mark_fn`**: If your struct contains `Object*` fields that
reference other Skeme objects, you must trace them so the GC doesn't collect them:

```c
static void mytype_mark(VM* pvm, Object* obj) {
    MyType* m = (MyType*)obj;
    markObject(pvm, m->some_object_field);  // keep it alive
}
```

**When to implement `free_fn`**: If your struct owns external resources (file
handles, malloc'd memory, etc.):

```c
static void mytype_free(VM* pvm, Object* obj) {
    MyType* m = (MyType*)obj;
    free(m->buffer);     // free sub-allocations
    close(m->fd);        // release OS resources
}
```

### Step 3: Register the type

```c
static SKTypeDesc fraction_desc = {
    .name     = "fraction",
    .mark_fn  = NULL,              // no Object* fields to trace
    .free_fn  = NULL,              // no sub-allocations to free
    .print_fn = fraction_print,
    .equal_fn = fraction_equal,
};

int FRACTION_TYPE = sk_register_type(pSK, &fraction_desc);
```

### Step 4: Create instances

```c
Fraction* f = (Fraction*)vm_newObject(&pSK->vm, sizeof(Fraction), FRACTION_TYPE);
f->numerator = 1;
f->denominator = 3;

// Push onto VM stack to make it accessible from Scheme
vm_push(&pSK->vm, (Object*)f);
```

### Step 5: Register builtin functions

Use `sk_register_functions()` to expose operations to Scheme:

```c
static int sk_make_fraction(SKState* pSK) {
    Object* denom_obj = vm_pop(&pSK->vm);
    Object* numer_obj = vm_pop(&pSK->vm);

    Fraction* f = (Fraction*)vm_newObject(&pSK->vm, sizeof(Fraction), FRACTION_TYPE);
    f->numerator = AS_NUM(numer_obj);
    f->denominator = AS_NUM(denom_obj);

    vm_push(&pSK->vm, (Object*)f);
    return SK_TRUE;
}

static int sk_fraction_p(SKState* pSK) {
    Object* obj = vm_pop(&pSK->vm);
    vm_push(&pSK->vm, OBJ_TYPE(obj) == FRACTION_TYPE ? MAKE_TRUE() : MAKE_FALSE());
    return SK_TRUE;
}

static SKFuncSet fraction_funcs[] = {
    { "make-fraction", sk_make_fraction, 2 },
    { "fraction?",     sk_fraction_p,    1 },
    { NULL, NULL, 0 }
};

sk_register_functions(pSK, fraction_funcs);
```

Then in Scheme:

```scheme
(define f (make-fraction 1 3))
(display f)           ; prints 1/3
(fraction? f)         ; => true
(typeof f)            ; => "fraction"
```

## GC Safety

When creating objects, be aware that any allocation (including `vm_newObject`)
can trigger garbage collection. To protect objects from being collected before
they are stored:

```c
// BAD — obj1 may be collected when obj2 is allocated
Fraction* obj1 = (Fraction*)vm_newObject(&vm, sizeof(Fraction), FRAC_TYPE);
Fraction* obj2 = (Fraction*)vm_newObject(&vm, sizeof(Fraction), FRAC_TYPE);

// GOOD — push obj1 onto the stack to protect it
Fraction* obj1 = (Fraction*)vm_newObject(&vm, sizeof(Fraction), FRAC_TYPE);
vm_push(&vm, (Object*)obj1);   // GC root
Fraction* obj2 = (Fraction*)vm_newObject(&vm, sizeof(Fraction), FRAC_TYPE);
vm_pop(&vm);                   // restore stack
```

## Type ID Details

- Built-in types use IDs `0` through `OBJ_LAST - 1`
- Custom types are assigned sequential IDs starting at `OBJ_LAST`
- Type IDs are stable for the lifetime of the VM
- The `Object.type` field is `uint8_t`, limiting the total number of types to 255
