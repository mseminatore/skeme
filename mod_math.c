#include "modules.h"
#include "vm.h"
#include "sk.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

// Helper: pop one numeric arg, return as double
static double pop_num(SKState* pSK, const char* fname)
{
    Object* obj = vm_pop(&pSK->vm);
    if (!IS_NUMERIC(obj)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "argument to '%s' must be a number", fname);
        yyerror(pSK, buf);
        return 0.0;
    }
    return AS_DOUBLE(obj);
}

// Helper: push a float result (returns int as int if exact)
static void push_result(SKState* pSK, double val)
{
    if (val == (int)val && val >= -1000000000 && val <= 1000000000)
        vm_push(&pSK->vm, MAKE_NUM((int)val));
    else
        vm_push(&pSK->vm, vm_new_float(&pSK->vm, (float)val));
}

// Trigonometric functions
static int sk_sin(SKState* pSK)   { push_result(pSK, sin(pop_num(pSK, "sin")));   return SK_TRUE; }
static int sk_cos(SKState* pSK)   { push_result(pSK, cos(pop_num(pSK, "cos")));   return SK_TRUE; }
static int sk_tan(SKState* pSK)   { push_result(pSK, tan(pop_num(pSK, "tan")));   return SK_TRUE; }
static int sk_asin(SKState* pSK)  { push_result(pSK, asin(pop_num(pSK, "asin"))); return SK_TRUE; }
static int sk_acos(SKState* pSK)  { push_result(pSK, acos(pop_num(pSK, "acos"))); return SK_TRUE; }
static int sk_atan(SKState* pSK)  { push_result(pSK, atan(pop_num(pSK, "atan"))); return SK_TRUE; }

static int sk_atan2(SKState* pSK)
{
    double x = pop_num(pSK, "atan2");
    double y = pop_num(pSK, "atan2");
    push_result(pSK, atan2(y, x));
    return SK_TRUE;
}

// Logarithmic / exponential
static int sk_math_log(SKState* pSK)   { push_result(pSK, log(pop_num(pSK, "log")));   return SK_TRUE; }
static int sk_math_log10(SKState* pSK) { push_result(pSK, log10(pop_num(pSK, "log10"))); return SK_TRUE; }
static int sk_math_exp(SKState* pSK)   { push_result(pSK, exp(pop_num(pSK, "exp")));   return SK_TRUE; }

static int sk_pow(SKState* pSK)
{
    double exp_val = pop_num(pSK, "pow");
    double base = pop_num(pSK, "pow");
    push_result(pSK, pow(base, exp_val));
    return SK_TRUE;
}

// Random numbers
static int seeded = 0;

static int sk_random(SKState* pSK)
{
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    // (random n) → int in [0, n)
    Object* obj = vm_pop(&pSK->vm);
    int n = (int)AS_DOUBLE(obj);
    vm_push(&pSK->vm, MAKE_NUM(n > 0 ? rand() % n : 0));
    return SK_TRUE;
}

static int sk_random_real(SKState* pSK)
{
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    // (random-real) → float in [0, 1)
    push_result(pSK, (double)rand() / ((double)RAND_MAX + 1.0));
    return SK_TRUE;
}

// Numeric predicates
static int sk_nan_p(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    vm_push(&pSK->vm, (IS_NUMERIC(obj) && isnan(AS_DOUBLE(obj))) ? MAKE_TRUE() : MAKE_FALSE());
    return SK_TRUE;
}

static int sk_infinite_p(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    vm_push(&pSK->vm, (IS_NUMERIC(obj) && isinf(AS_DOUBLE(obj))) ? MAKE_TRUE() : MAKE_FALSE());
    return SK_TRUE;
}

static int sk_finite_p(SKState* pSK)
{
    Object* obj = vm_pop(&pSK->vm);
    vm_push(&pSK->vm, (IS_NUMERIC(obj) && isfinite(AS_DOUBLE(obj))) ? MAKE_TRUE() : MAKE_FALSE());
    return SK_TRUE;
}

// Function table
static SKFuncSet math_funcs[] = {
    { "sin",       sk_sin,       1 },
    { "cos",       sk_cos,       1 },
    { "tan",       sk_tan,       1 },
    { "asin",      sk_asin,      1 },
    { "acos",      sk_acos,      1 },
    { "atan",      sk_atan,      1 },
    { "atan2",     sk_atan2,     2 },
    { "log",       sk_math_log,  1 },
    { "log10",     sk_math_log10, 1 },
    { "exp",       sk_math_exp,  1 },
    { "pow",       sk_pow,       2 },
    { "random",      sk_random,      1 },
    { "random-real", sk_random_real, 0 },
    { "nan?",      sk_nan_p,     1 },
    { "infinite?", sk_infinite_p, 1 },
    { "finite?",   sk_finite_p,  1 },
    { NULL, NULL, 0 }
};

// Module init — register functions and constants
int sk_math_init(SKState* pSK)
{
    sk_register_functions(pSK, math_funcs);

    // Define constants: pi, e
    Object* pi_key = vm_new_string(&pSK->vm, "pi");
    vm_push(&pSK->vm, pi_key);
    Object* pi_val = vm_new_float(&pSK->vm, (float)3.14159265358979323846);
    vm_define_global(&pSK->vm, vm_pop(&pSK->vm), pi_val);

    Object* e_key = vm_new_string(&pSK->vm, "e");
    vm_push(&pSK->vm, e_key);
    Object* e_val = vm_new_float(&pSK->vm, (float)2.71828182845904523536);
    vm_define_global(&pSK->vm, vm_pop(&pSK->vm), e_val);

    return 0;
}
