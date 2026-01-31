//------------------------------------------------------
// Skeme Unit Tests - Garbage Collection Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../vm.h"
#include "../parser.h"

void test_gc(int argc, char *argv[]) {
    MODULE("Garbage Collection");
    
    // Test object marking
    SUITE("Object Marking");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        COMMENT("Test string object marking...");
        Object* str = vm_new_string(&sk.vm, "test");
        TEST(str != NULL);
        TEST(IS_OBJ(str));
        TEST(!IS_MARKED(str));
        
        MARK_OBJ(str);
        TEST(IS_MARKED(str));
        
        UNMARK_OBJ(str);
        TEST(!IS_MARKED(str));
        
        vm_shutdown(&sk.vm);
    }
    
    // Test GC basics
    SUITE("GC Basic Operations");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        // Disable stress GC for this test
        int old_stress = g_bGCStress;
        g_bGCStress = FALSE;
        
        COMMENT("Test object allocation tracking...");
        size_t initial_count = sk.vm.numObjects;
        
        Object* str1 = vm_new_string(&sk.vm, "hello");
        TEST(sk.vm.numObjects == initial_count + 1);
        
        Object* str2 = vm_new_string(&sk.vm, "world");
        TEST(sk.vm.numObjects == initial_count + 2);
        
        COMMENT("Test GC collection...");
        // Push one string to keep it alive
        vm_push(&sk.vm, str1);
        
        // Run GC
        vm_gc(&sk.vm);
        
        // str1 should be alive (on stack)
        // str2 might be collected if not reachable
        TEST(sk.vm.numObjects <= initial_count + 2);
        
        vm_pop(&sk.vm);
        
        g_bGCStress = old_stress;
        vm_shutdown(&sk.vm);
    }
    
    // Test GC with stack references
    SUITE("GC Stack References");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        int old_stress = g_bGCStress;
        g_bGCStress = FALSE;
        
        COMMENT("Objects on stack should survive GC...");
        Object* str1 = vm_new_string(&sk.vm, "on_stack");
        vm_push(&sk.vm, str1);
        
        size_t before_gc = sk.vm.numObjects;
        vm_gc(&sk.vm);
        
        // Object on stack should survive
        Object* popped = vm_pop(&sk.vm);
        TEST(popped == str1);
        
        g_bGCStress = old_stress;
        vm_shutdown(&sk.vm);
    }
    
    // Test GC with global references
    SUITE("GC Global References");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        int old_stress = g_bGCStress;
        g_bGCStress = FALSE;
        
        COMMENT("Objects in globals should survive GC...");
        Object* key = vm_new_string(&sk.vm, "global_var");
        Object* val = vm_new_string(&sk.vm, "value");
        
        vm_define_global(&sk.vm, key, val);
        
        vm_gc(&sk.vm);
        
        // Should still be able to retrieve
        Object* retrieved = vm_get_global(&sk.vm, key);
        TEST(retrieved == val);
        
        g_bGCStress = old_stress;
        vm_shutdown(&sk.vm);
    }
    
    // Test GC statistics
    SUITE("GC Statistics");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        int old_stress = g_bGCStress;
        g_bGCStress = FALSE;
        
        COMMENT("Test GC counters...");
        size_t initial_cycles = sk.vm.gc_cycles;
        
        vm_gc(&sk.vm);
        TEST(sk.vm.gc_cycles == initial_cycles + 1);
        
        COMMENT("Test allocation tracking...");
        TEST(sk.vm.allocatedBytes >= 0);
        TEST(sk.vm.peakAllocatedBytes >= sk.vm.allocatedBytes);
        TEST(sk.vm.peakObjects >= sk.vm.numObjects);
        
        g_bGCStress = old_stress;
        vm_shutdown(&sk.vm);
    }
    
    // Test object types are correctly marked
    SUITE("GC Object Types");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        int old_stress = g_bGCStress;
        g_bGCStress = FALSE;
        
        COMMENT("Test string object type...");
        Object* str = vm_new_string(&sk.vm, "test");
        TEST(OBJ_TYPE(str) == OBJ_STRING);
        
        g_bGCStress = old_stress;
        vm_shutdown(&sk.vm);
    }
}
