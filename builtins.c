#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>
#include "sk.h"
#include "term_colors.h"
#include "platform.h"

#define SMALL_BUFFER 32
#define LARGE_BUFFER 1024

//
//[]-----------------------------------------------
//  built-in functions
//[]-----------------------------------------------
//

//-----------------------------------------------
// built-in function: newline
//-----------------------------------------------
static int sk_newline(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    FILE* stream = pSK->output_stream ? pSK->output_stream : stdout;
    fprintf(stream, "\n");
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: gclog
//-----------------------------------------------
static int sk_gclog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bGCLog = !g_bGCLog;
    yyinfo(pSK, g_bGCLog ? "GC logging enabled" : "GC logging disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: parselog
//-----------------------------------------------
static int sk_parselog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    pSK->parse_log_enabled = !pSK->parse_log_enabled;
    yyinfo(pSK, pSK->parse_log_enabled ? "Parser logging enabled" : "Parser logging disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: vmloh
//-----------------------------------------------
static int sk_vmlog(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    g_bVMLog = !g_bVMLog;
    yyinfo(pSK, g_bVMLog ? "VM logging enabled" : "VM logging disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: gc-stress
//-----------------------------------------------
static int sk_gc_stress(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    g_bGCStress = !g_bGCStress;
    yyinfo(pSK, g_bGCStress ? "GC stress testing enabled" : "GC stress testing disabled");
//    printf("GC stress mode %s\n", vm.gc_stress ? "enabled" : "disabled");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: null?
//-----------------------------------------------
static int sk_nullp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (IS_NIL(obj))
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: eq?
//-----------------------------------------------
static int sk_eqp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

    if (obj1 == obj2)
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: equal?
//-----------------------------------------------
static int sk_equalp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

	// check for pointer equality first, covers nil, boolean, interned string and number too
    if (obj1 == obj2)
    {
        bool = MAKE_TRUE();
        vm_push(&pSK->vm, bool);
		return SK_TRUE;
    }

	// check for string equality
    if (IS_STRING(obj1) && IS_STRING(obj2))
    {
        String* str1 = (String*)obj1;
        String* str2 = (String*)obj2;
        if (strcmp(str1->value, str2->value) == 0)
        {
            bool = MAKE_TRUE();
            vm_push(&pSK->vm, bool);
            return SK_TRUE;
        }
	}

	// checks (pairs, lists, etc.)
    if (IS_PAIR(obj1) && IS_PAIR(obj2))
    {
        Pair* pair1 = (Pair*)obj1;
        Pair* pair2 = (Pair*)obj2;

        // check car and cdr for equality
        vm_push(&pSK->vm, pair1->car);
        vm_push(&pSK->vm, pair2->car);
        sk_equalp(pSK);
        Object* car_equal = vm_pop(&pSK->vm);

        if (IS_FALSE(car_equal))
        {
            vm_push(&pSK->vm, bool);
            return SK_TRUE;
        }

        vm_push(&pSK->vm, pair1->cdr);
        vm_push(&pSK->vm, pair2->cdr);
        sk_equalp(pSK);
        Object* cdr_equal = vm_pop(&pSK->vm);
        if (IS_FALSE(cdr_equal))
        {
            vm_push(&pSK->vm, bool);
            return SK_TRUE;
        }

        // both car and cdr are equal
        bool = MAKE_TRUE();
	}

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: number?
//-----------------------------------------------
static int sk_numberp(SKState *pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (IS_NUM(obj))
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: boolean?
//-----------------------------------------------
static int sk_booleanp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (obj && IS_BOOL(obj))
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: char?
//-----------------------------------------------
static int sk_charp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* result = MAKE_FALSE();
    Object* obj = vm_pop(&pSK->vm);

    if (IS_CHAR(obj))
        result = MAKE_TRUE();

    vm_push(&pSK->vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: float?
//-----------------------------------------------
static int sk_floatp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* result = MAKE_FALSE();
    Object* obj = vm_pop(&pSK->vm);

    if (IS_FLOAT(obj))
        result = MAKE_TRUE();

    vm_push(&pSK->vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: char->integer
//-----------------------------------------------
static int sk_char_to_integer(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);

    if (!IS_CHAR(obj))
    {
        yyerror(pSK, "char->integer: expected character");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    int val = (unsigned char)AS_CHAR(obj);
    vm_push(&pSK->vm, MAKE_NUM(val));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: integer->char
//-----------------------------------------------
static int sk_integer_to_char(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);

    if (!IS_NUM(obj))
    {
        yyerror(pSK, "integer->char: expected integer");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    int val = AS_NUM(obj);
    if (val < 0 || val > 255)
    {
        yyerror(pSK, "integer->char: value out of range (0-255)");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    vm_push(&pSK->vm, MAKE_CHAR((char)val));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: pair?
//-----------------------------------------------
static int sk_pairp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cons
//-----------------------------------------------
static int sk_cons(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_peek(&pSK->vm, 0);     // need objects to stay live for now in case of GC
    Object* obj1 = vm_peek(&pSK->vm, 1);

	Pair* pair = (Pair*)vm_newObject(&pSK->vm, sizeof(Pair), OBJ_PAIR);    // might GC here!
    pair->car = obj1;
    pair->cdr = obj2;

    vm_pop(&pSK->vm);        // done with obj2
    vm_pop(&pSK->vm);        // done with obj1
    vm_push(&pSK->vm, (Object*)pair);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: not
//-----------------------------------------------
static int sk_not(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (IS_BOOL(obj) && IS_FALSE(obj))
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: car
//-----------------------------------------------
static int sk_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        result = ((Pair*)obj)->car;
    else
        yyerror(pSK, "argument to 'car' is not a pair");

    vm_push(&pSK->vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: cdr
//-----------------------------------------------
static int sk_cdr(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    Object* result = NULL;

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR)
        result = ((Pair*)obj)->cdr;
    else
        yyerror(pSK, "argument to 'cdr' is not a pair");

    vm_push(&pSK->vm, result);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: exit
//-----------------------------------------------
static int sk_exit(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // get arg count from stack
    Object* raw_n = vm_pop(&pSK->vm);
    int nargs = AS_NUM(raw_n);
    int exit_code = 0;

    if (nargs > 0)
    {
        Object* obj = vm_pop(&pSK->vm);
        if (obj && IS_NUM(obj))
        {
            exit_code = AS_NUM(obj);
        }
        else
        {
            yyerror(pSK, "invalid parameter to 'exit'");
        }
	}

    pSK->bDone = TRUE;

    printf(TERM_GREEN"\nExiting skeme interpreter with code (%d). Goodbye!\n\n"TERM_RESET, exit_code);

    vm_print_profile(&pSK->vm);

    exit(exit_code);
    return SK_TRUE;
}

//-----------------------------------------------
// error message helper
//-----------------------------------------------
static void error_helper(SKState* pSK)
{
    const char* msg;

    // get message count from stack
    Object* raw_n = vm_pop(&pSK->vm);
    int n = AS_NUM(raw_n);

    for (int i = n - 1; i >= 0; i--)
    {
        Object* obj = vm_peek(&pSK->vm, i);
        if (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
        {
            // Get the error message from the string object
            msg = ((String*)obj)->value;
            fprintf(stderr, "%s ", msg);
        }
    }

    vm_pop_n(&pSK->vm, n); // remove message strings from stack

    fputs("\n\n"TERM_RESET, stderr);
}

//-----------------------------------------------
// built-in function: error
//-----------------------------------------------
static int sk_error(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, TERM_RED"Error: ");
    error_helper(pSK);
    
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: warn
//-----------------------------------------------
static int sk_warn(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, TERM_YELLOW"Warning: ");
    error_helper(pSK);
    
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: warn
//-----------------------------------------------
static int sk_info(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, TERM_GREEN"Info: ");
    error_helper(pSK);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string?
//-----------------------------------------------
static int sk_stringp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

    if (obj && IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
        bool = MAKE_TRUE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: symbol?
//-----------------------------------------------
static int sk_symbolp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    Object* obj = vm_pop(&pSK->vm);
    vm_push(&pSK->vm, IS_SYMBOL(obj) ? MAKE_TRUE() : MAKE_FALSE());
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: symbol->string
//-----------------------------------------------
static int sk_symbol_to_string(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_SYMBOL(obj)) {
        yyerror(pSK, "symbol->string: expected a symbol");
        vm_push(&pSK->vm, MAKE_FALSE());
        return SK_FALSE;
    }
    vm_push(&pSK->vm, vm_new_string(&pSK->vm, ((String*)obj)->value));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string->symbol
//-----------------------------------------------
static int sk_string_to_symbol(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_STRING(obj)) {
        yyerror(pSK, "string->symbol: expected a string");
        vm_push(&pSK->vm, MAKE_FALSE());
        return SK_FALSE;
    }
    vm_push(&pSK->vm, vm_new_symbol(&pSK->vm, ((String*)obj)->value));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: list?
//-----------------------------------------------
static int sk_listp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);

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

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// force a gc
//-----------------------------------------------
static int sk_gc(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_gc(&pSK->vm);

    return SK_TRUE;
}

//-----------------------------------------------
// display gc stats
//-----------------------------------------------
static int sk_gc_stats(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    fprintf(stderr, "\t Live objects: %zu (%zu peak)\n\tObj Collected: %zu\n\tBytes alloc'd: %zu (%zu peak)\n\t    GC passes: %zu\n\t   Next GC at: %zu bytes\n",
        pSK->vm.numObjects,
        pSK->vm.peakObjects,
        pSK->vm.objectsCollected,
        pSK->vm.allocatedBytes,
        pSK->vm.peakAllocatedBytes,
        pSK->vm.gc_cycles,
        pSK->vm.nextGC
    );

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: zero?
//-----------------------------------------------
static int sk_zerop(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = MAKE_FALSE();   // default to false
    Object* obj = vm_pop(&pSK->vm);
    
    if (obj && IS_NUM(obj) && AS_NUM(obj) == 0)
        bool = MAKE_TRUE();
    
    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: atom?
//-----------------------------------------------
int sk_atomp(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* bool = vm_newBoolean(&pSK->vm, TRUE);   // default to true
    Object* obj = vm_pop(&pSK->vm);
    
    if (IS_NIL(obj) || (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_PAIR))
        bool = MAKE_FALSE();

    vm_push(&pSK->vm, bool);
    return SK_TRUE;
}

//-----------------------------------------------
// helper for binary ops
//-----------------------------------------------
static int sk_mathop(SKState* pSK, char op)
{
    if (!pSK) return SK_FALSE;

    Object* raw_n = vm_pop(&pSK->vm);
    int n = AS_NUM(raw_n);

    // First pass: check if any argument is a float
    int has_float = FALSE;
    for (int i = 0; i < n; i++)
    {
        Object* obj = vm_peek(&pSK->vm, i);
        if (!IS_NUMERIC(obj))
        {
            yyerror(pSK, "arguments to mathop must be numbers");
            return SK_FALSE;
        }
        if (IS_FLOAT(obj))
            has_float = TRUE;
    }

    if (has_float)
    {
        // Float path: compute in double precision
        Object* obj1 = vm_peek(&pSK->vm, n - 1);
        double result = AS_DOUBLE(obj1);
        int logic_result = TRUE;

        for (int i = n - 2; i >= 0; i--)
        {
            Object* obj2 = vm_peek(&pSK->vm, i);
            double val2 = AS_DOUBLE(obj2);

            switch (op)
            {
            case '+':
                result = result + val2;
                break;
            case '-':
                result = result - val2;
                break;
            case '*':
                result = result * val2;
                break;
            case '/':
                if (val2 == 0.0)
                {
                    yyerror(pSK, "division by zero");
                    return SK_FALSE;
                }
                result = result / val2;
                break;
            case '=':
                if (result != val2)
                    logic_result = FALSE;
                break;
            }
        }

        vm_pop_n(&pSK->vm, n);
        if (op != '=')
            vm_push(&pSK->vm, MAKE_TAGGED_FLOAT((float)result));
        else
            vm_push(&pSK->vm, logic_result ? MAKE_TRUE() : MAKE_FALSE());
    }
    else
    {
        // Integer path: compute in int
        Object* obj1 = vm_peek(&pSK->vm, n - 1);
        int result = AS_NUM(obj1);
        int logic_result = TRUE;

        for (int i = n - 2; i >= 0; i--)
        {
            Object* obj2 = vm_peek(&pSK->vm, i);
            int val2 = AS_NUM(obj2);

            switch (op)
            {
            case '+':
                result = result + val2;
                break;
            case '-':
                result = result - val2;
                break;
            case '*':
                result = result * val2;
                break;
            case '/':
                if (val2 == 0)
                {
                    yyerror(pSK, "division by zero");
                    return SK_FALSE;
                }
                result = result / val2;
                break;
            case '=':
                if (result != val2)
                    logic_result = FALSE;
                break;
            }
        }

        vm_pop_n(&pSK->vm, n);
        if (op != '=')
            vm_push(&pSK->vm, MAKE_NUM(result));
        else
            vm_push(&pSK->vm, logic_result ? MAKE_TRUE() : MAKE_FALSE());
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

    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

    if (!IS_NUMERIC(obj1) || !IS_NUMERIC(obj2))
    {
        yyerror(pSK, "arguments to '<' must be numbers");
        return SK_FALSE;
	}

    double val1 = AS_DOUBLE(obj1);
    double val2 = AS_DOUBLE(obj2);
    vm_push(&pSK->vm, val1 < val2 ? MAKE_TRUE() : MAKE_FALSE());

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

    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

    if (!IS_NUMERIC(obj1) || !IS_NUMERIC(obj2))
    {
        yyerror(pSK, "arguments to '<=' must be numbers");
        return SK_FALSE;
    }

    double val1 = AS_DOUBLE(obj1);
    double val2 = AS_DOUBLE(obj2);
    vm_push(&pSK->vm, val1 <= val2 ? MAKE_TRUE() : MAKE_FALSE());

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: >
//-----------------------------------------------
static int sk_greater(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

    if (!IS_NUMERIC(obj1) || !IS_NUMERIC(obj2))
    {
        yyerror(pSK, "arguments to '>' must be numbers");
        return SK_FALSE;
    }

    double val1 = AS_DOUBLE(obj1);
    double val2 = AS_DOUBLE(obj2);
    vm_push(&pSK->vm, val1 > val2 ? MAKE_TRUE() : MAKE_FALSE());

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: >=
//-----------------------------------------------
static int sk_greater_equal(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj2 = vm_pop(&pSK->vm);
    Object* obj1 = vm_pop(&pSK->vm);

    if (!IS_NUMERIC(obj1) || !IS_NUMERIC(obj2))
    {
        yyerror(pSK, "arguments to '>=' must be numbers");
        return SK_FALSE;
    }

    double val1 = AS_DOUBLE(obj1);
    double val2 = AS_DOUBLE(obj2);
    vm_push(&pSK->vm, val1 >= val2 ? MAKE_TRUE() : MAKE_FALSE());

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
    while(ht_next(pSK->vm.interned_strings, &index, &key, &value))
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
    fprintf(stdout, "Global objects:\n");
    while (ht_next(pSK->vm.global_table, &index, &key, &value))
    {
		String* strKey = (String*)key;
        fprintf(stdout, "%10s\t", (const char*)strKey->value);
        vm_print_object((Object*)value, VM_PRINT_FANCY, stdout);
        fputs("\n", stdout);
        count++;
    }

    fprintf(stdout, "%d globals total.\n", count);

    return SK_TRUE;
}
//-----------------------------------------------
// built-in function: pwd
//-----------------------------------------------
static int sk_pwd(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	char cwd[SK_PATH_MAX];
	if (NULL == sk_getcwd(cwd, sizeof(cwd)))
		yyerror(pSK, "failed to get current working directory");
    else
        fprintf(stdout, "Current working directory: '%s'\n", cwd);

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

    Object* obj = vm_pop(&pSK->vm);
    if (!obj)
    {
        vm_push(&pSK->vm, NULL); // length 0
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
            yyerror(pSK, "argument to 'length' is not a list or string");
        }
    }
    else
    {
        yyerror(pSK, "argument to 'length' is not a list or string");
    }

    vm_push(&pSK->vm, MAKE_NUM(count)); // push length as number object
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: set-car!
//-----------------------------------------------
static int sk_set_car(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // implement set-car!
    Object *obj = vm_peek(&pSK->vm, 1);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_PAIR)
    {
        vm_pop(&pSK->vm); // remove values
        vm_pop(&pSK->vm);
        yyerror(pSK, "argument to 'set-car!' is not a pair");
        return SK_FALSE;
    }

    Pair* pair = (Pair*)obj;
    Object* new_car = vm_pop(&pSK->vm); // get new car value
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
    Object *obj = vm_peek(&pSK->vm, 1);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_PAIR)
    {
        vm_pop(&pSK->vm); // remove values
        vm_pop(&pSK->vm);
        yyerror(pSK, "argument to 'set-cdr!' is not a pair");
        return SK_FALSE;
    }

    Pair* pair = (Pair*)obj;
    Object* new_cdr = vm_pop(&pSK->vm); // get new cdr value
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

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_PROCEDURE)
    {
        if (IS_FUNCTION(obj))
            yyerror(pSK, "argument to 'disasm' is a native function");
        else
            yyerror(pSK, "argument to 'disasm' is not a function");

        return SK_FALSE;
    }

    Procedure* proc = (Procedure*)obj;
    vm_disassemble_chunk(&pSK->vm, &proc->chunk, proc->name ? proc->name->value : "<anonymous>");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: print stack
//-----------------------------------------------
static int sk_print_stack(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_print_stack(&pSK->vm);

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
    
    Object *key = vm_pop(&pSK->vm);
    if (!IS_OBJ(key) || OBJ_TYPE(key) != OBJ_STRING)
    {
        yyerror(pSK, "argument to 'getenv' is not a string");
        return SK_FALSE;
    }

    const char *str = ((String*)key)->value;
    const char *value = getenv(str);
    if (value)
    {
        vm_push_string(&pSK->vm, value);
    }
    else
    {
        vm_push(&pSK->vm, NULL);
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: command-line
//-----------------------------------------------
static int sk_command_line(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	vm_push_global(&pSK->vm, "__env__");

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: typeof
//-----------------------------------------------
static int sk_typeof(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    Object* obj = vm_pop(&pSK->vm);
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
            if (OBJ_TYPE(((Pair*)obj)->cdr) == OBJ_PAIR || IS_NIL(((Pair*)obj)->cdr))
                type_name = "list";
            else
                type_name = "pair";
            break;

        case OBJ_PROCEDURE:
            type_name = "procedure";
            break;

        case OBJ_NATIVE_FN:
            type_name = "native-function";
            break;

        case OBJ_FLOAT:
            type_name = "float";
            break;

        case OBJ_CLOSURE:
            type_name = "closure";
            break;

        case OBJ_ENVIRONMENT:
            type_name = "environment";
            break;

        case OBJ_UPVALUE:
            type_name = "upvalue";
            break;

        case OBJ_CHAR:
            type_name = "char";
            break;

        case OBJ_PORT:
            type_name = "file";
            break;

        default:
        {
            const SKTypeDesc* desc = vm_get_type_desc(&pSK->vm, OBJ_TYPE(obj));
            type_name = desc ? desc->name : "object";
        }
            break;
        }
    }

    vm_push_string(&pSK->vm, type_name);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: mem-allocated
//-----------------------------------------------
static int sk_mem_allocated(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    vm_push(&pSK->vm, MAKE_NUM((int)pSK->vm.allocatedBytes));

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: map
//-----------------------------------------------
static int sk_map(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // Get nargs - format: (map fn list1 list2 ...) 
    // Stack after nargs pop: [fn, list1, list2, ..., listN]
    // Where listN is at top (index 0), fn is at bottom (index nargs-1)
    Object* raw_n = vm_pop(&pSK->vm);
    int nargs = AS_NUM(raw_n);
    int num_lists = nargs - 1;  // first arg is fn, rest are lists

    if (num_lists < 1)
    {
        yyerror(pSK, "map: requires at least one list");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    // Support up to 16 lists
    Object* lists[16];
    if (num_lists > 16)
    {
        yyerror(pSK, "map: too many lists (max 16)");
        vm_pop_n(&pSK->vm, nargs);
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    // Peek at fn (at bottom of args)
    Object* fn = vm_peek(&pSK->vm, nargs - 1);
    if (!IS_FUNCTION(fn) && !IS_CLOSURE(fn))
    {
        yyerror(pSK, "map: first argument must be a function");
        vm_pop_n(&pSK->vm, nargs);
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    // Collect lists - list1 is at index num_lists-1, listN is at index 0
    for (int i = 0; i < num_lists; i++)
    {
        lists[i] = vm_peek(&pSK->vm, num_lists - 1 - i);  // list1 at lists[0]
        if (!IS_PAIR(lists[i]) && !IS_NIL(lists[i]))
        {
            yyerror(pSK, "map: arguments after function must be lists");
            vm_pop_n(&pSK->vm, nargs);
            vm_push(&pSK->vm, NULL);
            return SK_FALSE;
        }
    }

    // Pop all args now that we've validated
    vm_pop_n(&pSK->vm, nargs);

    // Handle empty lists - if any list is empty, return empty
    for (int i = 0; i < num_lists; i++)
    {
        if (IS_NIL(lists[i]))
        {
            vm_push(&pSK->vm, NULL);
            return SK_TRUE;
        }
    }

    // Build result list
    Object* result_head = NULL;
    Pair* result_tail = NULL;

    // Protect fn and all lists from GC
    vm_push(&pSK->vm, fn);
    for (int i = 0; i < num_lists; i++)
        vm_push(&pSK->vm, lists[i]);

    // Iterate while all lists have elements
    int all_have_elements = TRUE;
    while (all_have_elements)
    {
        // Check all lists still have elements
        for (int i = 0; i < num_lists; i++)
        {
            // Re-read from stack in case GC moved them
            lists[i] = vm_peek(&pSK->vm, num_lists - 1 - i);
            if (!IS_PAIR(lists[i]))
            {
                all_have_elements = FALSE;
                break;
            }
        }
        if (!all_have_elements) break;

        // Gather car of each list as args
        Object* args[16];
        for (int i = 0; i < num_lists; i++)
        {
            Pair* p = (Pair*)lists[i];
            args[i] = p->car;
        }

        // Call the function with gathered args
        fn = vm_peek(&pSK->vm, num_lists);  // re-read fn from stack
        Object* mapped = vm_call_function(&pSK->vm, fn, num_lists, args);

        // Protect mapped and result_head before allocation
        vm_push(&pSK->vm, mapped);
        vm_push(&pSK->vm, result_head);

        // Allocate new pair
        Pair* new_pair = (Pair*)vm_newObject(&pSK->vm, sizeof(Pair), OBJ_PAIR);

        // Restore protected values
        result_head = vm_pop(&pSK->vm);
        mapped = vm_pop(&pSK->vm);

        new_pair->car = mapped;
        new_pair->cdr = NULL;

        // Append to result list
        if (result_head == NULL) {
            result_head = (Object*)new_pair;
            result_tail = new_pair;
        } else {
            result_tail->cdr = (Object*)new_pair;
            result_tail = new_pair;
        }

        // Advance all lists to cdr and update stack protection
        for (int i = 0; i < num_lists; i++)
        {
            lists[i] = vm_peek(&pSK->vm, num_lists - 1 - i);
            Pair* p = (Pair*)lists[i];
            lists[i] = p->cdr;
            vm_stack_replace_n(&pSK->vm, num_lists - 1 - i, lists[i]);
        }
    }

    // Pop all protected values (lists + fn)
    vm_pop_n(&pSK->vm, num_lists + 1);

    vm_push(&pSK->vm, result_head);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: eval
//-----------------------------------------------
static int sk_eval(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    yyerror(pSK, "built-in function 'eval' not implemented yet");
    return SK_FALSE;
}

//-----------------------------------------------
// built-in function: number-to-string
//-----------------------------------------------
static int sk_number_to_string(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUM(obj))
    {
        yyerror(pSK, "number-to-string expects a number");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    int num = AS_NUM(obj);
    char buffer[SMALL_BUFFER];
    snprintf(buffer, sizeof(buffer), "%d", num);
    vm_push_string(&pSK->vm, buffer);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string-to-number
//-----------------------------------------------
static int sk_string_to_number(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_STRING)
    {
        yyerror(pSK, "string-to-number expects a string");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    String* str = (String*)obj;
    int num = atoi(str->value);
    vm_push(&pSK->vm, MAKE_NUM(num));

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string->float
//-----------------------------------------------
static int sk_string_to_float(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_STRING)
    {
        yyerror(pSK, "string->float expects a string");
        vm_push(&pSK->vm, MAKE_FALSE());
        return SK_FALSE;
    }

    String* str = (String*)obj;
    char* endptr;
    float val = (float)strtod(str->value, &endptr);
    
    if (endptr == str->value)
    {
        // Parse failed - no conversion performed
        vm_push(&pSK->vm, MAKE_FALSE());
    }
    else
    {
        vm_push(&pSK->vm, vm_new_float(&pSK->vm, val));
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: float->string
//-----------------------------------------------
static int sk_float_to_string(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_FLOAT(obj))
    {
        yyerror(pSK, "float->string expects a float");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    float val = vm_get_float(obj);
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", val);
    
    Object* str = vm_new_string(&pSK->vm, buf);
    vm_push(&pSK->vm, str);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: delete-file
//-----------------------------------------------
static int sk_delete_file(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_OBJ(obj) || OBJ_TYPE(obj) != OBJ_STRING)
    {
        yyerror(pSK, "delete-file expects a string filename");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    String* filename = (String*)obj;
    int result = remove(filename->value);
    if (result == 0)
    {
        vm_push(&pSK->vm, MAKE_TRUE());
    }
    else
    {
        yyerror(pSK, "failed to delete file");
        vm_push(&pSK->vm, MAKE_FALSE());
    }

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: read-line
//-----------------------------------------------
static int sk_read_line(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Port *port = (Port*) vm_pop(&pSK->vm);
    if (!IS_OBJ(port) || !IS_PORT(port) || !port->fptr)
    {
        yyerror(pSK, "read-line expects a valid port object");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    char buffer[LARGE_BUFFER] = { 0 };
    if (fgets(buffer, sizeof(buffer), port->fptr) == NULL)
    {
        // EOF or error
        vm_push(&pSK->vm, NULL); // push nil
    }
    else
    {
        // remove newline character if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
        {
            buffer[len - 1] = '\0';
        }
        vm_push_string(&pSK->vm, buffer);
    }   

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: read-char (port is optional, defaults to yyin)
//-----------------------------------------------
static int sk_read_char(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // get arg count from stack
    Object* raw_n = vm_pop(&pSK->vm);
    int nargs = AS_NUM(raw_n);
    
    FILE* fptr;
    if (nargs == 0)
    {
        // No port provided, use default input
        fptr = pSK->yyin ? pSK->yyin : stdin;
    }
    else
    {
        Object* port_obj = vm_pop(&pSK->vm);
        if (!IS_PORT(port_obj))
        {
            yyerror(pSK, "read-char: expected port");
            vm_push(&pSK->vm, NULL);
            return SK_FALSE;
        }
        Port* port = (Port*)port_obj;
        if (!port->fptr)
        {
            yyerror(pSK, "read-char: port is closed");
            vm_push(&pSK->vm, NULL);
            return SK_FALSE;
        }
        fptr = port->fptr;
    }

    int c = fgetc(fptr);
    if (c == EOF)
    {
        vm_push(&pSK->vm, NULL);  // return nil on EOF
    }
    else
    {
        vm_push(&pSK->vm, MAKE_CHAR((char)c));
    }
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: write-char (port is optional, defaults to output_stream)
//-----------------------------------------------
static int sk_write_char(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    // get arg count from stack
    Object* raw_n = vm_pop(&pSK->vm);
    int nargs = AS_NUM(raw_n);

    if (nargs < 1)
    {
        yyerror(pSK, "write-char: expected at least one argument");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    FILE* fptr;
    if (nargs == 1)
    {
        // No port provided, use default output
        fptr = pSK->output_stream ? pSK->output_stream : stdout;
    }
    else
    {
        Object* port_obj = vm_pop(&pSK->vm);
        if (!IS_PORT(port_obj))
        {
            yyerror(pSK, "write-char: expected port");
            vm_push(&pSK->vm, NULL);
            return SK_FALSE;
        }
        Port* port = (Port*)port_obj;
        if (!port->fptr)
        {
            yyerror(pSK, "write-char: port is closed");
            vm_push(&pSK->vm, NULL);
            return SK_FALSE;
        }
        fptr = port->fptr;
    }

    Object* char_obj = vm_pop(&pSK->vm);
    if (!IS_CHAR(char_obj))
    {
        yyerror(pSK, "write-char: expected character");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    fputc(AS_CHAR(char_obj), fptr);
    vm_push(&pSK->vm, NULL);  // return nil
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: write-line
//-----------------------------------------------
static int sk_write_line(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Port *port = (Port*) vm_pop(&pSK->vm);
    if (!IS_OBJ(port) || !IS_PORT(port) || !port->fptr)
    {
        yyerror(pSK, "write-line expects a valid port object");

        // replace output object with return value
        vm_stack_replace_n(&pSK->vm, 0, NULL);

        return SK_FALSE;
    }

    String* str = (String*) vm_pop(&pSK->vm);
    if (!IS_OBJ(str) || OBJ_TYPE(str) != OBJ_STRING)
    {
        yyerror(pSK, "write-line expects a string");
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    // TODO - check return value
    fprintf(port->fptr, "%s\n", str->value);

    vm_push(&pSK->vm, MAKE_TRUE());
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: read
//-----------------------------------------------
static int sk_read(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    yyerror(pSK, "built-in function 'read' not implemented yet");
    return SK_FALSE;
}

//-----------------------------------------------
// built-in function: write
//-----------------------------------------------
static int sk_write(SKState* pSK)
{
    if (!pSK) return SK_FALSE;
    
    Port* port = (Port*)vm_pop(&pSK->vm);
    if (!IS_OBJ(port) || !IS_PORT(port) || !port->fptr)
    {
        yyerror(pSK, "write expects a valid port object");
        
        // replace output object with return value
        vm_stack_replace_n(&pSK->vm, 0, NULL);

        return SK_FALSE;
    }

	vm_print_object(vm_pop(&pSK->vm), VM_PRINT_READABLE, port->fptr);

    vm_push(&pSK->vm, MAKE_TRUE());
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string-append
//-----------------------------------------------
static int sk_string_append(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

	char buffer[LARGE_BUFFER] = { 0 };

    // get message count from stack
    Object* raw_n = vm_pop(&pSK->vm);
    int n = AS_NUM(raw_n);

    for (int i = n - 1; i >= 0; i--)
    {
        Object* obj = vm_peek(&pSK->vm, i);
        if (IS_OBJ(obj) && OBJ_TYPE(obj) == OBJ_STRING)
        {
            // Get the error message from the string object
			strcat(buffer, ((String*)obj)->value);
        }
        else
            {
            yyerror(pSK, "arguments to 'string-append' must be strings");
            vm_pop_n(&pSK->vm, n);            // remove message strings from stack
			vm_push(&pSK->vm, NULL);          // push nil
            return SK_FALSE;
		}
    }

    vm_pop_n(&pSK->vm, n);               // remove message strings from stack
	vm_push_string(&pSK->vm, buffer);    // push concatenated string

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: printf
//-----------------------------------------------
static int sk_printf(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    yyerror(pSK, "built-in function 'printf' not implemented yet");
    return SK_FALSE;
}

//-----------------------------------------------
//
//-----------------------------------------------
static int sk_modulo(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* modulus_obj = vm_pop(&pSK->vm);
    Object* dividend_obj = vm_pop(&pSK->vm);

    if (!dividend_obj || !IS_NUM(dividend_obj) || !modulus_obj || !IS_NUM(modulus_obj))
    {
        yyerror(pSK, "arguments to modulo must be numbers");
        return SK_FALSE;
    }

    int dividend = AS_NUM(dividend_obj);
    int modulus = AS_NUM(modulus_obj);
    if (modulus == 0)
	{
		yyerror(pSK, "modulo by zero");
		return SK_FALSE;
	}

    vm_push(&pSK->vm, MAKE_NUM(dividend % modulus));

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: open-file
//-----------------------------------------------
static int sk_open_file(SKState* pSK, char *mode)
{
    if (!pSK) return SK_FALSE;

    String* name = (String*)vm_peek(&pSK->vm, 0);
    if (!IS_STRING(name))
    {
        yyerror(pSK, "open-file expects a string filename");
        vm_pop(&pSK->vm);
        vm_push(&pSK->vm, NULL);
        return SK_FALSE;
    }

    Port* port = (Port*)vm_newObject(&pSK->vm, sizeof(Port), OBJ_PORT);
    port->fptr = NULL;
    port->name = NULL;

    FILE* fptr = fopen(name->value, mode);
	if (!fptr)
	{
		yyerror(pSK, "failed to open file");
		vm_pop(&pSK->vm);           // remove filename
		vm_push(&pSK->vm, NULL);     // push nil
		return SK_FALSE;
	}

	port->fptr = fptr;          // store the file pointer
    port->name = name;          // store the filename
    vm_pop(&pSK->vm);                // remove filename from stack
    vm_push(&pSK->vm, (Object*)port);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: open-input-file
//-----------------------------------------------
static int sk_open_input_file(SKState* pSK)
{
    return sk_open_file(pSK, "r");
}

//-----------------------------------------------
// built-in function: open-output-file
//-----------------------------------------------
static int sk_open_output_file(SKState* pSK)
{
    return sk_open_file(pSK, "w");
}

//-----------------------------------------------
// built-in function: close-file
//-----------------------------------------------
static int sk_close_file(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object* obj = vm_pop(&pSK->vm);
    if (!IS_OBJ(obj) || !IS_PORT(obj))
	{
		yyerror(pSK, "close-file expects a file object");
        vm_push(&pSK->vm, MAKE_FALSE());
		return SK_FALSE;
	}

    Port* port = (Port*)obj;
    if (port->fptr)
	{
		fclose(port->fptr);
		port->fptr = NULL;
        port->name = NULL;
	}

    vm_push(&pSK->vm, MAKE_TRUE());
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: reverse
//-----------------------------------------------
static int sk_reverse(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    Object *obj = vm_peek(&pSK->vm, 0);
    if (IS_NIL(obj))
    {
        return SK_TRUE;
    }

    if (!IS_PAIR(obj))
    {
		yyerror(pSK, "argument to 'reverse' is not a list");
        vm_pop(&pSK->vm);
		return SK_FALSE;
    }

    Pair* current = (Pair*)obj;
    Pair* reversed = NULL;
    vm_push(&pSK->vm, NULL); // push dummy on stack to keep new list alive in case of GC

    while (current && IS_PAIR(current))
	{
		Pair* new_pair = (Pair*)vm_newObject(&pSK->vm, sizeof(Pair), OBJ_PAIR);
        vm_pop(&pSK->vm);            // remove dummy
		new_pair->car = current->car;
		new_pair->cdr = (Object*)reversed;
		reversed = new_pair;
        vm_push(&pSK->vm, (Object*)reversed);  // keep alive in case of GC
		current = (Pair*)current->cdr;
	}
    
    vm_pop_n(&pSK->vm, 2);                   // remove original list and dummy
    vm_push(&pSK->vm, (Object*)reversed);

    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: abs
//-----------------------------------------------
static int sk_abs(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (IS_FLOAT(obj)) {
        double v = AS_DOUBLE(obj);
        vm_push(&pSK->vm, vm_new_float(&pSK->vm, (float)(v < 0 ? -v : v)));
    } else if (IS_NUM(obj)) {
        int v = AS_NUM(obj);
        vm_push(&pSK->vm, MAKE_NUM(v < 0 ? -v : v));
    } else {
        yyerror(pSK, "argument to 'abs' must be a number");
        return SK_FALSE;
    }
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: quotient
//-----------------------------------------------
static int sk_quotient(SKState* pSK)
{
    Object* b = vm_pop(&pSK->vm);
    Object* a = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(a) || !IS_NUMERIC(b)) {
        yyerror(pSK, "arguments to 'quotient' must be numbers");
        return SK_FALSE;
    }
    int bv = IS_FLOAT(b) ? (int)AS_DOUBLE(b) : AS_NUM(b);
    if (bv == 0) { yyerror(pSK, "division by zero"); return SK_FALSE; }
    int av = IS_FLOAT(a) ? (int)AS_DOUBLE(a) : AS_NUM(a);
    vm_push(&pSK->vm, MAKE_NUM(av / bv));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: remainder
//-----------------------------------------------
static int sk_remainder(SKState* pSK)
{
    Object* b = vm_pop(&pSK->vm);
    Object* a = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(a) || !IS_NUMERIC(b)) {
        yyerror(pSK, "arguments to 'remainder' must be numbers");
        return SK_FALSE;
    }
    int bv = IS_FLOAT(b) ? (int)AS_DOUBLE(b) : AS_NUM(b);
    if (bv == 0) { yyerror(pSK, "division by zero"); return SK_FALSE; }
    int av = IS_FLOAT(a) ? (int)AS_DOUBLE(a) : AS_NUM(a);
    vm_push(&pSK->vm, MAKE_NUM(av % bv));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: expt (exponentiation)
//-----------------------------------------------
static int sk_expt(SKState* pSK)
{
    Object* b = vm_pop(&pSK->vm);
    Object* a = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(a) || !IS_NUMERIC(b)) {
        yyerror(pSK, "arguments to 'expt' must be numbers");
        return SK_FALSE;
    }
    if (IS_NUM(a) && IS_NUM(b) && AS_NUM(b) >= 0) {
        // Integer fast path for non-negative exponent
        int base = AS_NUM(a), exp = AS_NUM(b), result = 1;
        for (int i = 0; i < exp; i++) result *= base;
        vm_push(&pSK->vm, MAKE_NUM(result));
    } else {
        double result = pow(AS_DOUBLE(a), AS_DOUBLE(b));
        vm_push(&pSK->vm, vm_new_float(&pSK->vm, (float)result));
    }
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: sqrt
//-----------------------------------------------
static int sk_sqrt(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'sqrt' must be a number");
        return SK_FALSE;
    }
    double result = sqrt(AS_DOUBLE(obj));
    // Return integer if result is exact
    if (IS_NUM(obj) && result == (int)result)
        vm_push(&pSK->vm, MAKE_NUM((int)result));
    else
        vm_push(&pSK->vm, vm_new_float(&pSK->vm, (float)result));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: floor
//-----------------------------------------------
static int sk_floor(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'floor' must be a number");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_NUM((int)floor(AS_DOUBLE(obj))));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: ceiling
//-----------------------------------------------
static int sk_ceiling(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'ceiling' must be a number");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_NUM((int)ceil(AS_DOUBLE(obj))));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: truncate
//-----------------------------------------------
static int sk_truncate(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'truncate' must be a number");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_NUM((int)AS_DOUBLE(obj)));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: round
//-----------------------------------------------
static int sk_round(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'round' must be a number");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_NUM((int)round(AS_DOUBLE(obj))));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: positive?
//-----------------------------------------------
static int sk_positivep(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'positive?' must be a number");
        return SK_FALSE;
    }
    vm_pushBoolean(&pSK->vm, AS_DOUBLE(obj) > 0);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: negative?
//-----------------------------------------------
static int sk_negativep(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        yyerror(pSK, "argument to 'negative?' must be a number");
        return SK_FALSE;
    }
    vm_pushBoolean(&pSK->vm, AS_DOUBLE(obj) < 0);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: procedure?
//-----------------------------------------------
static int sk_procedurep(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    vm_pushBoolean(&pSK->vm, IS_FUNCTION(obj) || IS_CLOSURE(obj));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string-length
//-----------------------------------------------
static int sk_string_length(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_STRING(obj)) {
        yyerror(pSK, "argument to 'string-length' must be a string");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_NUM((int)strlen(((String*)obj)->value)));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string-ref
//-----------------------------------------------
static int sk_string_ref(SKState* pSK)
{
    Object* idx_obj = vm_pop(&pSK->vm);
    Object* str_obj = vm_pop(&pSK->vm);
    if (!IS_STRING(str_obj) || !IS_NUM(idx_obj)) {
        yyerror(pSK, "string-ref requires a string and an integer index");
        return SK_FALSE;
    }
    String* s = (String*)str_obj;
    int idx = AS_NUM(idx_obj);
    int len = (int)strlen(s->value);
    if (idx < 0 || idx >= len) {
        yyerror(pSK, "string-ref: index out of range");
        return SK_FALSE;
    }
    vm_push(&pSK->vm, MAKE_CHAR(s->value[idx]));
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: substring
//-----------------------------------------------
static int sk_substring(SKState* pSK)
{
    Object* end_obj = vm_pop(&pSK->vm);
    Object* start_obj = vm_pop(&pSK->vm);
    Object* str_obj = vm_pop(&pSK->vm);
    if (!IS_STRING(str_obj) || !IS_NUM(start_obj) || !IS_NUM(end_obj)) {
        yyerror(pSK, "substring requires a string and two integer indices");
        return SK_FALSE;
    }
    String* s = (String*)str_obj;
    int start = AS_NUM(start_obj);
    int end = AS_NUM(end_obj);
    int len = (int)strlen(s->value);
    if (start < 0 || end < start || end > len) {
        yyerror(pSK, "substring: index out of range");
        return SK_FALSE;
    }
    char buf[LARGE_BUFFER];
    int sub_len = end - start;
    if (sub_len >= LARGE_BUFFER) sub_len = LARGE_BUFFER - 1;
    memcpy(buf, s->value + start, sub_len);
    buf[sub_len] = '\0';
    vm_push_string(&pSK->vm, buf);
    return SK_TRUE;
}

//-----------------------------------------------
// built-in function: string-copy
//-----------------------------------------------
static int sk_string_copy(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_STRING(obj)) {
        yyerror(pSK, "argument to 'string-copy' must be a string");
        return SK_FALSE;
    }
    vm_push_string(&pSK->vm, ((String*)obj)->value);
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
	{ "cls", sk_cls, 0 },
    { "open-input-file", sk_open_input_file, 1 },
    { "open-output-file", sk_open_output_file, 1 },
    { "close-file", sk_close_file, 1 },
    { "delete-file", sk_delete_file, 1 },
    { "read", sk_read, 1 },
    { "read-line", sk_read_line, 1 },
    { "read-char", sk_read_char, SK_ARITY_ZERO_OR_ONE },
    { "write-char", sk_write_char, SK_ARITY_ONE_OR_MORE },
    { "write-line", sk_write_line, 2 },
    { "write", sk_write, 2 },
    // { "print", sk_print },
    
    { "string-append", sk_string_append, SK_ARITY_TWO_OR_MORE },
	{ "printf", sk_printf, SK_ARITY_ONE_OR_MORE },

    // predicate functions
    { "null?", sk_nullp, 1 },
    { "eq?", sk_eqp, 2},
    { "eqv?", sk_eqp, 2},
    { "equal?", sk_equalp, 2},
    { "number?", sk_numberp, 1 },
    { "boolean?", sk_booleanp, 1 },
    { "char?", sk_charp, 1 },
    { "float?", sk_floatp, 1 },
    { "pair?", sk_pairp, 1},
    { "string?", sk_stringp, 1 },
    { "list?", sk_listp, 1 },
    { "zero?", sk_zerop, 1 },
    { "atom?", sk_atomp, 1 },
	{ "integer?", sk_numberp, 1 }, // TODO - replace alias for number?

    // logic
    { "not", sk_not, 1 },

    // list functions
    { "length", sk_length, 1 },
    { "set-car!", sk_set_car, 2 },
    { "set-cdr!", sk_set_cdr, 2 },
    { "cons", sk_cons, 2 },
    { "car", sk_car, 1 },
    { "cdr", sk_cdr, 1 },
    { "reverse", sk_reverse, 1},
    //{ "append", sk_append, SK_ARITY_TWO_OR_MORE },

    // memory management
    { "gc", sk_gc, 0 },
    { "gc-stats", sk_gc_stats, 0 },
    
    // control functions
    { "exit", sk_exit, SK_ARITY_ZERO_OR_ONE },

    // higher-order functions
    { "map", sk_map, SK_ARITY_TWO_OR_MORE },
    { "eval", sk_eval, 1 },

    // system/environment functions
	{ "pwd", sk_pwd, 0 },
	{ "mem-allocated", sk_mem_allocated, 0 },
    { "getenv", sk_getenv, 1 },
    //{ "setenv", sk_setenv, 2 },
    { "command-line", sk_command_line, 0 },

    // logging functions
    { "gclog", sk_gclog, 0 },
	{ "parselog", sk_parselog, 0 },
	{ "vmlog", sk_vmlog, 0 },

    // converstion functions
    { "number->string", sk_number_to_string, 1 },
    { "string->number", sk_string_to_number, 1 },
    { "char->integer", sk_char_to_integer, 1 },
    { "integer->char", sk_integer_to_char, 1 },
    { "float->string", sk_float_to_string, 1 },
    { "string->float", sk_string_to_float, 1 },
    { "string->symbol", sk_string_to_symbol, 1 },
    { "symbol->string", sk_symbol_to_string, 1 },

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
    { "modulo", sk_modulo, 2},
    { "abs", sk_abs, 1 },
    { "quotient", sk_quotient, 2 },
    { "remainder", sk_remainder, 2 },
    { "expt", sk_expt, 2 },
    { "sqrt", sk_sqrt, 1 },
    { "floor", sk_floor, 1 },
    { "ceiling", sk_ceiling, 1 },
    { "truncate", sk_truncate, 1 },
    { "round", sk_round, 1 },
    { "positive?", sk_positivep, 1 },
    { "negative?", sk_negativep, 1 },
    { "procedure?", sk_procedurep, 1 },

    // string functions
    { "string-length", sk_string_length, 1 },
    { "string-ref", sk_string_ref, 2 },
    { "substring", sk_substring, 3 },
    { "string-copy", sk_string_copy, 1 },

	// debugging functions
    { "disasm", sk_disasm, 1 },
	{ "print-stack", sk_print_stack, 0 },
    { "strings", sk_strings, 0 },
    { "globals", sk_globals },
	{ "typeof", sk_typeof, 1 },
    { "gc-stress", sk_gc_stress, 0 },

	{ "symbol?", sk_symbolp, 1 },
	// { "eval", sk_eval },
    { NULL, NULL }
};

//-----------------------------------------------
// register built-in functions
//-----------------------------------------------
int sk_init_builtins(SKState* pSK)
{
    if (!pSK) return SK_FALSE;

    sk_register_functions(pSK, built_in_funcs);
    
	// create default ports
	vm_create_default_ports(&pSK->vm);

    return SK_TRUE;
}
