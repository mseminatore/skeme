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
        switch (obj->type)
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
                printf("(");

                printObject(pair->car); // display car

                Object* cdr = pair->cdr;
                while (cdr && IS_OBJ(cdr) && cdr->type == OBJ_PAIR)
                {
                    pair = (Pair*)cdr;
                    printf(" ");
                    printObject(pair->car); // display car
                    cdr = pair->cdr;
                }

                if (cdr && !IS_NIL(cdr))
                {
                    printf(" . ");
                    printObject(pair->car); // display car
                }
                printf(")\n");
            }
			break;

        case OBJ_FUNCTION:
        case OBJ_NATIVE_FN:
            printf("#<procedure>\n");
			break;

   //     case OBJ_BUILTIN_FN:
			//printf("#<builtin fn>\n");
			//break;

        default:
            printf("#<obj type=%d>\n", obj->type);
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
    printf(TERM_RESET);

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
// built-in function: null?
//-----------------------------------------------
static int sk_nullp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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
    
    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
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

	Pair* pair = (Pair*)vm_newObject(&vm, sizeof(Pair));    // might GC here!
    pair->base.type = OBJ_PAIR;
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

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
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

    if (obj && IS_OBJ(obj) && obj->type == OBJ_PAIR)
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
// built-in function: error
//-----------------------------------------------
static int sk_error(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    const char* error_msg = "error called";

	// get error message from stack
    Object* obj = vm_pop(&vm);
    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
    {
        // Get the error message from the string object
        error_msg = ((String*)obj)->value;
    }

    // simply raise a parser error for now
    yyerror(error_msg);
    
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: define
//-----------------------------------------------
//int sk_define(SKState* pSK)
//{
//    if (!pSK) return SK_FALSE;
//
//    // implement define
//    Object* obj_value = vm_peek(&vm, 0);
//    const char* key = (const char*)vm_peek(&vm, 1);
//
//    Object *obj_key = vm_newString(&vm, key);
//
//    // Add the new global variable
//    vm_define_global(&vm, obj_key, obj_value);
//
//	vm_pop(&vm);
//    vm_pop(&vm);        // done with values
//    return SK_TRUE;
//}

//-----------------------------------------------
// built-in function: string?
//-----------------------------------------------
static int sk_stringp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);

    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
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

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
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
            if (current->type == OBJ_PAIR)
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

    fprintf(stderr, "\tLive objects: %d (%zu peak)\n\tBytes alloc'd: %zu (%zu peak)\n\tGC passes: %zu\n\tNext GC at %zu bytes\n",
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
    if (obj && IS_OBJ(obj) && obj->type == OBJ_STRING)
    {
        // Get the filename from the string object
        const char* filename = ((String*)obj)->value;
        push_file_stack(filename);
        yyparse();
    }
    else
        yyerror("invalid parameter");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: if
//-----------------------------------------------
static int sk_if(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement if

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: zero?
//-----------------------------------------------
static int sk_zerop(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    Object* obj = vm_pop(&vm);
    
    if (obj && IS_NUM(obj) && AS_NUM(obj) == 0)
        bool = MAKE_TRUE();
    
    vm_push(&vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: <
//-----------------------------------------------
static int sk_less(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj1 = vm_peek(&vm, 0);
    Object* obj2 = vm_peek(&vm, 1);

    if (!obj1 || !IS_NUM(obj1) || !obj2 || !IS_NUM(obj2))
    {
        yyerror("arguments to '<' must be numbers");
        return SK_FALSE;
	}

    Object* bool = vm_newBoolean(&vm, FALSE);   // default to false, might GC here!
    if (AS_NUM(obj1) < AS_NUM(obj2))
        bool = MAKE_TRUE();

    vm_pop(&vm);        // done with obj
    vm_pop(&vm);        // done with obj
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

    // print interned strings
    printf("Interned strings:\n");
    while(ht_next(vm.interned_strings, &index, &key, &value))
    {
        printf("  '%s'\n", (const char*)key);
    }

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
// built-in function: list
//-----------------------------------------------
static int sk_list(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // TODO - implement list creation
    // pop the number of elements from the stack
    // int count = vm_pop(&vm) ? AS_NUM(vm_pop(&vm)) : 0;
    
    // create a linked list of pairs
    // push the list object onto the stack

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
        if (obj->type == OBJ_PAIR)
        {
            // count list elements
            Object* current = obj;
            while (current && IS_OBJ(current) && current->type == OBJ_PAIR)
            {
                count++;
                current = ((Pair*)current)->cdr;
            }
        }
        else if (obj->type == OBJ_STRING)
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
    if (!obj || !IS_OBJ(obj) || obj->type != OBJ_PAIR)
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
    if (!obj || !IS_OBJ(obj) || obj->type != OBJ_PAIR)
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
// built-in function: set!
//-----------------------------------------------
static int sk_set(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    // implement set!
    Object* obj_value = vm_peek(&vm, 0);
    Object* obj_key = vm_peek(&vm, 1);
    if (!obj_key)
    {
        yyerror("valid identifier expected!");
        return SK_FALSE;
    }

    // Update the global variable
    vm_update_global(&vm, obj_key, obj_value);
    vm_pop(&vm);
    vm_pop(&vm);        // done with values

    return SK_TRUE;
}

//-----------------------------------------------
// built-in functions
//-----------------------------------------------
static SKFuncSet built_in_funcs[] =
{
    { "display", sk_display },
    { "null?", sk_nullp },
    { "eq?", sk_eqp },
    { "number?", sk_numberp },
    { "boolean?", sk_booleanp },
    { "pair?", sk_pairp },
    { "cons", sk_cons },
    { "not", sk_not },
    { "car", sk_car },
    { "cdr", sk_cdr },
    { "exit", sk_exit },
	{ "error", sk_error },
    { "string?", sk_stringp },
    { "list?", sk_listp },
    { "gc", sk_gc },
    { "gc-stats", sk_gc_stats },
    { "load", sk_load },
    { "zero?", sk_zerop },
    { "strings", sk_strings },
	{ "pwd", sk_pwd },
    { "list", sk_list },
    { "length", sk_length },
    { "set-car!", sk_set_car },
    { "set-cdr!", sk_set_cdr },
    { "set!", sk_set },
    { "newline", sk_newline },
    { "gclog", sk_gclog },
	{ "parselog", sk_parselog },

    //{ "if", sk_if },
    //{ "<", sk_less, },
    // { "list", sk_list },
    // { "print", sk_print },
    // { "atom?", sk_atomp },
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
