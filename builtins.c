#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "parser.h"
#include "vm.h"
#include "term_colors.h"

#ifdef __linux
    #include <limits.h> // for PATH_MAX
#endif

#ifdef __APPLE__
    #include <sys/syslimits.h> // for PATH_MAX
#endif

#ifdef _WIN32
    #include <direct.h> // for _getcwd
#else
    #include <unistd.h> // for getcwd
#endif

#ifdef _WIN32
    #define PATH_MAX _MAX_PATH
    #define getcwd _getcwd
#endif

extern int g_bVMLog;

//
//[]-----------------------------------------------
//  built-in functions
//[]-----------------------------------------------
//

//-----------------------------------------------
// print an object
//-----------------------------------------------
static void printObject(Object *obj)
{
    if (!obj)
    {
        printf("'()");   // nil
        return;
    }

    if (IS_BOOL(obj))
    {
        if (IS_TRUE(obj))
            printf("#t");
        else
            printf("#f");

        return;
    }

    if (IS_OBJ(obj))
    {
        switch (OBJ_TYPE(obj))
        {
        case OBJ_STRING:
            {
                String* strObj = (String*)obj;
                printf("\"%s\"", strObj->value);
            }
            break;

        case OBJ_PAIR:
            {
                Pair* pair = (Pair*)obj;
                printf("'(");

                printObject(pair->car); // display car

                Object* cdr = pair->cdr;
                while (cdr && IS_OBJ(cdr) && OBJ_TYPE(cdr) == OBJ_PAIR)
                {
                    pair = (Pair*)cdr;
                    printf(" ");
                    printObject(pair->car); // display car
                    cdr = pair->cdr;
                }

                if (cdr && !IS_NIL(cdr))
                {
                    printf(" . ");
                    printObject(pair->cdr); // display cdr
                }
                printf(")");
            }
			break;

        case OBJ_FUNCTION:
            printf("#<procedure:%s>", ((String*)((Procedure*)obj)->name)->value);
			break;

        case OBJ_NATIVE_FN:
            printf("#<native_fn:%s>", ((String*)((NativeFunction*)obj)->name)->value);
			break;

        default:
            printf("#<obj type=%d>", OBJ_TYPE(obj));
            break;
        }
    }
    else
    {
        // print number
        printf("%d", AS_NUM(obj));
    }
}

//-----------------------------------------------
// built-in function: display
//-----------------------------------------------
int sk_display(SKState *pSK)
{
	if (!pSK) return SK_FALSE;

    // pop the top of the stack and print it
    Object *obj = vm_pop(&vm);

    printf(TERM_BRIGHT_BLUE);
        printObject(obj);
    printf(TERM_RESET"\n");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: newline
//-----------------------------------------------
static int sk_newline(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    puts("");   // newline
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: gclog
//-----------------------------------------------
static int sk_gclog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bGCLog = !g_bGCLog;
    yyinfo(g_bGCLog ? "GC logging enabled" : "GC logging disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: parselog
//-----------------------------------------------
static int sk_parselog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bParseLog = !g_bParseLog;
    yyinfo(g_bParseLog ? "Parser logging enabled" : "Parser logging disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: vmloh
//-----------------------------------------------
static int sk_vmlog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bVMLog = !g_bVMLog;
    yyinfo(g_bVMLog ? "VM logging enabled" : "VM logging disabled");

    return SK_TRUE;
}
//-----------------------------------------------
// built-in function: null?
//-----------------------------------------------
static int sk_nullp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (IS_NIL(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: eq?
//-----------------------------------------------
static int sk_eqp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (obj1 == obj2)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: number?
//-----------------------------------------------
static int sk_numberp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (IS_NUM(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: boolean?
//-----------------------------------------------
static int sk_booleanp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (obj && IS_BOOL(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: pair?
//-----------------------------------------------
static int sk_pairp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cons
//-----------------------------------------------
static int sk_cons(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_peek(&vm, 0);     // need objects to stay live for now in case of GC
    Object* obj1 = vm_peek(&vm, 1);

	Pair* pair = (Pair*)vm_newObject(&vm, sizeof(Pair), OBJ_PAIR);    // might GC here!
    pair->car = obj1;
    pair->cdr = obj2;

    vm_pop(&vm);        // done with obj2
    vm_pop(&vm);        // done with obj1
    vm_push(&vm, (Object*)pair);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: not
//-----------------------------------------------
static int sk_not(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (IS_BOOL(obj) && IS_FALSE(obj))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: car
//-----------------------------------------------
static int sk_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        result = ((Pair*)obj)->car;
    else
        yyerror("argument to 'car' is not a pair");

    vm_push(&vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cdr
//-----------------------------------------------
static int sk_cdr(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        result = ((Pair*)obj)->cdr;
    else
        yyerror("argument to 'cdr' is not a pair");

    vm_push(&vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: exit
//-----------------------------------------------
static int sk_exit(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bDone = TRUE;

    printf(TERM_GREEN"\nExiting skeme interpreter. Goodbye!\n\n"TERM_RESET);

    //exit(0);
    return SK_TRUE;
}

//-----------------------------------------------
// error message helper
//-----------------------------------------------
static void error_helper()
{
    const char* msg;

    // get message count from stack
    Object* raw_n = vm_pop(&vm);
    int n = AS_NUM(raw_n);

    for (int i = n - 1; i >= 0; i--)
    {
        Object* obj = vm_peek(&vm, i);
        if (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
        {
            // Get the error message from the string object
            msg = ((String*)obj)->value;
            fprintf(stderr, "%s ", msg);
        }
    }

    vm_pop_n(&vm, n); // remove message strings from stack

    fputs("\n\n"TERM_RESET, stderr);
}

//-----------------------------------------------
// built-in function: error
//-----------------------------------------------
static int sk_error(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	fprintf(stderr, TERM_RED"Error: ");
    error_helper();
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: warn
//-----------------------------------------------
static int sk_warn(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, TERM_YELLOW"Warning: ");
    error_helper();
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: warn
//-----------------------------------------------
static int sk_info(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, TERM_GREEN"Info: ");
    error_helper();
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string?
//-----------------------------------------------
static int sk_stringp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
        bool = MAKE_TRUE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: list?
//-----------------------------------------------
static int sk_listp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);

    // check for proper list (nil or chain of pairs ending in nil)
    if (obj == NULL)
    {
        bool = MAKE_TRUE();
    }
    else
    {
        Object* current = obj;
        int is_list = FALSE;
        while (current && IS_OBJ(current))
        {
            if (OBJ_TYPE(current) == OBJ_PAIR)
            {
                current = ((Pair*)current)->cdr;
                if (IS_NIL(current))
                {
                    is_list = TRUE;
                    break;  // end of list
                }
            }
            else
            {
                is_list = FALSE;
                break;
            }
        }
        if (is_list)
            bool = MAKE_TRUE();
    }

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// force a gc
//-----------------------------------------------
static int sk_gc(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_gc(&vm);

    return SK_TRUE;
}

//-----------------------------------------------
// display gc stats
//-----------------------------------------------
static int sk_gc_stats(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, "\t Live objects: %zu (%zu peak)\n\tBytes alloc'd: %zu (%zu peak)\n\t    GC passes: %zu\n\t   Next GC at: %zu bytes\n",
        vm.numObjects,
        vm.peakObjects,
        vm.allocatedBytes,
        vm.peakAllocatedBytes,
        vm.gc_cycles,
        vm.nextGC
    );

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: load
//-----------------------------------------------
static int sk_load(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement load
	Object* obj = vm_pop(&vm);
    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
    {
        // Get the filename from the string object
        const char* filename = ((String*)obj)->value;
        push_filename_stack(filename);
        // yyparse(pSK);
    }
    else
        yyerror("invalid parameter");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: zero?
//-----------------------------------------------
static int sk_zerop(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    Object* obj = vm_pop(&vm);
    
    if (obj && IS_NUM(obj) && AS_NUM(obj) == 0)
        bool = MAKE_TRUE();
    
    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: atom?
//-----------------------------------------------
int sk_atomp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, TRUE);   // default to true
    Object* obj = vm_pop(&vm);
    
    if (IS_NIL(obj) || (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR))
        bool = MAKE_FALSE();

    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// helper for binary ops
//-----------------------------------------------
static int sk_mathop(SKState* pSK, char op)
{
    if (!pSK) return SK_FALSE;

    Object* raw_n = vm_pop(&vm);
    int n = AS_NUM(raw_n);

    Object * obj1 = vm_peek(&vm, n - 1);   // get first term
    int result = AS_NUM(obj1);
    if (!IS_NUM(obj1))
    {
        yyerror("arguments to mathop must be numbers");
        return SK_FALSE;
	}

    for (int i = n - 2; i >= 0; i--)
    {
        Object* obj2 = vm_peek(&vm, i);
        if (!IS_NUM(obj2))
        {
            yyerror("arguments to mathop must be numbers");
            return SK_FALSE;
        }

        switch (op)
        {
        case '+':
			result = result + AS_NUM(obj2);
            break;

        case '-':
            result = result - AS_NUM(obj2);
            break;

        case '*':
            result = result * AS_NUM(obj2);
            break;

        case '/':
            if (AS_NUM(obj2) == 0)
            {
                yyerror("division by zero");
                return SK_FALSE;
            }
            result = result / AS_NUM(obj2);
            break;

        case '=':
            {
                if (result != AS_NUM(obj2))
                {
                    result = FALSE;
                }
            }
            break;
        }
	}

    vm_pop_n(&vm, n); // remove all args from stack
    if (op != '=')
		vm_push(&vm, MAKE_NUM(result));
    else
    {
        vm_push(&vm, result == FALSE ? MAKE_FALSE() : MAKE_TRUE());
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: +
//-----------------------------------------------
static int sk_add(SKState* pSK)
{
    return sk_mathop(pSK, '+');
}

//-----------------------------------------------
// built-in function: -
//-----------------------------------------------
static int sk_subtract(SKState* pSK)
{
    return sk_mathop(pSK, '-');
}

//-----------------------------------------------
// built-in function: *
//-----------------------------------------------
static int sk_multiply(SKState* pSK)
{
    return sk_mathop(pSK, '*');
}

//-----------------------------------------------
// built-in function: /
//-----------------------------------------------
static int sk_divide(SKState* pSK)
{
    return sk_mathop(pSK, '/');
}

//-----------------------------------------------
// built-in function: <
//-----------------------------------------------
static int sk_less(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '<' must be numbers");
        return SK_FALSE;
	}

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    if (AS_NUM(obj1) < AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: =
//-----------------------------------------------
static int sk_equalnum(SKState* pSK)
{
    return sk_mathop(pSK, '=');
}

//-----------------------------------------------
// built-in function: <=
//-----------------------------------------------
static int sk_less_equal(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '<=' must be numbers");
        return SK_FALSE;
    }

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    if (AS_NUM(obj1) <= AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: >
//-----------------------------------------------
static int sk_greater(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '>' must be numbers");
        return SK_FALSE;
    }

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    if (AS_NUM(obj1) > AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: >
//-----------------------------------------------
static int sk_greater_equal(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&vm);
    Object* obj1 = vm_pop(&vm);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '>=' must be numbers");
        return SK_FALSE;
    }

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false
    if (AS_NUM(obj1) >= AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_push(&vm, bool);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: strings
//-----------------------------------------------
static int sk_strings(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    size_t index = 0;
    ht_key_t key;
    ht_value_t value;
    int count = 0;

    // print interned strings
    printf("Interned strings:\n");
    while(ht_next(vm.interned_strings, &index, &key, &value))
    {
        printf("  '%s'\n", (const char*)key);
        count++;
    }

    printf("%d strings total.\n", count);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: strings
//-----------------------------------------------
static int sk_globals(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    size_t index = 0;
    ht_key_t key;
    ht_value_t value;
    int count = 0;

    // print interned strings
    printf("Global objects:\n");
    while (ht_next(vm.global_table, &index, &key, &value))
    {
		String* strKey = (String*)key;
        printf("%10s\t", (const char*)strKey->value);
        printObject((Object*)value);
        puts("");
        count++;
    }

    printf("%d globals total.\n", count);

    return SK_TRUE;
}
//-----------------------------------------------
// built-in function: pwd
//-----------------------------------------------
static int sk_pwd(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	char cwd[PATH_MAX];
	if (NULL == getcwd(cwd, sizeof(cwd)))
		yyerror("failed to get current working directory");
    else
        printf("Current working directory: '%s'\n", cwd);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: length
//-----------------------------------------------
static int sk_length(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // length function for lists and strings
    int count = 0;

    Object* obj = vm_pop(&vm);
    if (!obj)
    {
        vm_push(&vm, NULL); // length 0
        return SK_TRUE;
    }

    if (IS_OBJ(obj))
    {
        if (OBJ_TYPE(obj) == OBJ_PAIR)
        {
            // count list elements
            Object* current = obj;
            while (current && IS_OBJ(current) && OBJ_TYPE(current) == OBJ_PAIR)
            {
                count++;
                current = ((Pair*)current)->cdr;
            }
        }
        else if (OBJ_TYPE(obj) == OBJ_STRING)
        {
            String* strObj = (String*)obj;
            count = (int)strlen(strObj->value);
        }
        else
        {
            yyerror("argument to 'length' is not a list or string");
        }
    }
    else
    {
        yyerror("argument to 'length' is not a list or string");
    }

    vm_push(&vm, MAKE_NUM(count)); // push length as number object
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: set-car!
//-----------------------------------------------
static int sk_set_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement set-car!
    Object *obj = vm_peek(&vm, 1);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_PAIR)
    {
        vm_pop(&vm); // remove values
        vm_pop(&vm);
        yyerror("argument to 'set-car!' is not a pair");
        return SK_FALSE;
    }

    Pair* pair = (Pair*)obj;
    Object* new_car = vm_pop(&vm); // get new car value
    pair->car = new_car;

    // leave pair on stack

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: set-cdr!
//-----------------------------------------------
static int sk_set_cdr(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement set-car!
    Object *obj = vm_peek(&vm, 1);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_PAIR)
    {
        vm_pop(&vm); // remove values
        vm_pop(&vm);
        yyerror("argument to 'set-cdr!' is not a pair");
        return SK_FALSE;
    }

    Pair* pair = (Pair*)obj;
    Object* new_cdr = vm_pop(&vm); // get new cdr value
    pair->cdr = new_cdr;

    // leave pair on stack

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: disasm
//-----------------------------------------------
static int sk_disasm(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&vm);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_FUNCTION)
    {
        yyerror("argument to 'disasm' is not a function");
        return SK_FALSE;
    }

    Procedure* proc = (Procedure*)obj;
    vm_disassemble_chunk(&vm, &proc->chunk, proc->name ? ((String*)proc->name)->value : "<anonymous>");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: print stack
//-----------------------------------------------
static int sk_print_stack(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_print_stack(&vm);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: clear screen
//-----------------------------------------------
static int sk_cls(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // clear the screen
    puts(TERM_CLEAR);   // "\033[2J\033[H"
	return SK_TRUE;
}

//-----------------------------------------------
// built-in function: getenv
//-----------------------------------------------
static int sk_getenv(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object *key = vm_pop(&vm);
    if (!IS_OBJ(key) || OBJ_TYPE(key) != OBJ_STRING)
    {
        yyerror("argument to 'getenv' is not a string");
        return SK_FALSE;
    }

    const char *str = ((String*)key)->value;
    const char *value = getenv(str);
    if (value)
    {
        vm_push_string(&vm, value);
    }
    else
    {
        vm_push(&vm, NULL);
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: command-line
//-----------------------------------------------
static int sk_command_line(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    //vm_push(&vm, vm_get_global(&vm, vm_new_string(&vm, "env__")));
	vm_push_global(&vm, "env__");
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: typeof
//-----------------------------------------------
static int sk_typeof(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    Object* obj = vm_pop(&vm);
    const char* type_name = "unknown";

    if (!obj)
    {
        type_name = "nil";
    }
    else if (IS_BOOL(obj))
    {
        type_name = "boolean";
    }
    else if (IS_NUM(obj))
    {
        type_name = "number";
    }
    else if (IS_OBJ(obj))
    {
        switch (OBJ_TYPE(obj))
        {
        case OBJ_STRING:
            type_name = "string";
            break;

        case OBJ_PAIR:
            type_name = "pair";
            break;

        case OBJ_FUNCTION:
            type_name = "function";
            break;

        case OBJ_NATIVE_FN:
            type_name = "native-function";
            break;

        default:
            type_name = "object";
            break;
        }
    }

    vm_push_string(&vm, type_name);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: mem-allocated
//-----------------------------------------------
static int sk_mem_allocated(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    vm_push(&vm, MAKE_NUM((int)vm.allocatedBytes));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in functions
//-----------------------------------------------
static SKFuncSet built_in_funcs[] =
{
    // I/O functions
    { "display", sk_display, 1 },
    { "newline", sk_newline, 0 },
	{ "error", sk_error, SK_ARITY_ONE_OR_MORE },
	{ "warn", sk_warn, SK_ARITY_ONE_OR_MORE },
	{ "info", sk_info, SK_ARITY_ONE_OR_MORE },
    { "load", sk_load, 1 },
	{ "cls", sk_cls, 0 },

    // environment functions
	{ "getenv", sk_getenv, 1 }, 
	//{ "setenv", sk_setenv, 2 },
	{ "command-line", sk_command_line, 0 },
	{ "mem-allocated", sk_mem_allocated, 0 },

    // predicate functions
    { "null?", sk_nullp, 1 },
    { "eq?", sk_eqp, 2},
    { "number?", sk_numberp, 1 },
    { "boolean?", sk_booleanp, 1 },
    { "pair?", sk_pairp, 1},
    { "string?", sk_stringp, 1 },
    { "list?", sk_listp, 1 },
    { "zero?", sk_zerop, 1 },
    { "atom?", sk_atomp, 1 },

    // list functions
    { "length", sk_length, 1 },
    { "set-car!", sk_set_car, 2 },
    { "set-cdr!", sk_set_cdr, 2 },
    { "cons", sk_cons, 2 },
    { "not", sk_not, 1 },
    { "car", sk_car, 1 },
    { "cdr", sk_cdr, 1 },

    { "exit", sk_exit, 0 },
    { "gc", sk_gc, 0 },
	{ "pwd", sk_pwd, 0 },

    // logging functions
    { "gclog", sk_gclog, 0 },
	{ "parselog", sk_parselog, 0 },
	{ "vmlog", sk_vmlog, 0 },

    // comparison operators
    { "<", sk_less, 2 },
	{ ">", sk_greater, 2 },
	{ "<=", sk_less_equal, 2 },
	{ ">=", sk_greater_equal, 2 },

    // arithmetic operators
    { "+", sk_add, SK_ARITY_TWO_OR_MORE },
    { "-", sk_subtract, SK_ARITY_TWO_OR_MORE },
    { "*", sk_multiply, SK_ARITY_TWO_OR_MORE },
	{ "/", sk_divide, SK_ARITY_TWO_OR_MORE },
    { "=", sk_equalnum, SK_ARITY_TWO_OR_MORE },

	// debugging functions
    { "disasm", sk_disasm, 1 },
	{ "print-stack", sk_print_stack, 0 },
    { "strings", sk_strings, 0 },
    { "globals", sk_globals },
    { "gc-stats", sk_gc_stats, 0 },
	{ "typeof", sk_typeof, 1 },

    // { "print", sk_print },
    // { "integer?", sk_integerp },
    // { "real?", sk_realp },
    // { "float?", sk_floatp },
    // { "char?", sk_charp },
	// { "symbol?", sk_symbolp },
	// { "read", sk_read },
	// { "eval", sk_eval },
	// { "map", sk_map },
	// { "filter", sk_filter },
    { NULL, NULL }
};

//-----------------------------------------------
//
//-----------------------------------------------
int sk_init_builtins(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    sk_register_functions(pSK, built_in_funcs);
    
    return SK_TRUE;
}
