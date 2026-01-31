//------------------------------------------------------
// Skeme Unit Tests - Builtins Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../parser.h"
#include "../vm.h"

void test_builtins(int argc, char *argv[]) {
    MODULE("Builtins");
    
    // Test builtin initialization
    SUITE("Builtin Initialization");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        
        COMMENT("Test sk_init registers builtins...");
        int result = sk_init(&sk, 0, NULL);
        // sk_init should succeed and include builtin registration
        TEST(result != 0);
        
        // Check that some builtins are registered
        Object* key = vm_new_string(&sk.vm, "+");
        Object* plus_fn = vm_get_global(&sk.vm, key);
        TEST(plus_fn != NULL);
        TEST(IS_NATIVE_FN(plus_fn));
        
        COMMENT("Test multiple builtins registered...");
        key = vm_new_string(&sk.vm, "-");
        Object* minus_fn = vm_get_global(&sk.vm, key);
        TEST(minus_fn != NULL);
        TEST(IS_NATIVE_FN(minus_fn));
        
        key = vm_new_string(&sk.vm, "cons");
        Object* cons_fn = vm_get_global(&sk.vm, key);
        TEST(cons_fn != NULL);
        TEST(IS_NATIVE_FN(cons_fn));
        
        key = vm_new_string(&sk.vm, "car");
        Object* car_fn = vm_get_global(&sk.vm, key);
        TEST(car_fn != NULL);
        TEST(IS_NATIVE_FN(car_fn));
        
        sk_shutdown(&sk);
    }
    
    // Test that common builtin functions exist
    SUITE("Builtin Function Lookup");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        const char* builtin_names[] = {
            "+", "-", "*", "/", "=",
            "cons", "car", "cdr",
            "number?", "string?", "pair?", "list?", "null?", "boolean?", "atom?",
            "length", "reverse",
            "display", "newline",
            "gc", "gc-stats",
            NULL
        };
        
        COMMENT("Test builtin function presence...");
        for (int i = 0; builtin_names[i] != NULL; i++) {
            Object* key = vm_new_string(&sk.vm, builtin_names[i]);
            Object* fn = vm_get_global(&sk.vm, key);
            TEST(fn != NULL);
            TEST(IS_NATIVE_FN(fn));
        }
        
        sk_shutdown(&sk);
    }
    
    // Test native function structure
    SUITE("Native Function Structure");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test native function has name...");
        Object* key = vm_new_string(&sk.vm, "+");
        Object* plus_fn = vm_get_global(&sk.vm, key);
        TEST(plus_fn != NULL);
        
        NativeFunction* native = (NativeFunction*)plus_fn;
        TEST(native->name != NULL);
        TEST(native->func != NULL);
        
        sk_shutdown(&sk);
    }
}
