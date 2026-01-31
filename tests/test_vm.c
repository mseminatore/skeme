//------------------------------------------------------
// Skeme Unit Tests - VM Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../vm.h"
#include "../parser.h"

void test_vm(int argc, char *argv[]) {
    MODULE("VM");
    
    // Test VM initialization and shutdown
    SUITE("VM Initialization");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        
        TEST(vm_init(&sk.vm) == VM_OK);
        TEST(sk.vm.sp == 0);
        TEST(sk.vm.fp == 0);
        TEST(sk.vm.return_sp == 0);
        TEST(sk.vm.numObjects == 0);
        TEST(sk.vm.head == NULL);
        TEST(sk.vm.global_table != NULL);
        
        vm_shutdown(&sk.vm);
        TEST(sk.vm.global_table == NULL);
    }
    
    // Test stack operations
    SUITE("Stack Operations");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        COMMENT("Test push and pop...");
        Object* num1 = MAKE_NUM(42);
        vm_push(&sk.vm, num1);
        TEST(sk.vm.sp == 1);
        
        Object* popped = vm_pop(&sk.vm);
        TEST(popped == num1);
        TEST(sk.vm.sp == 0);
        
        COMMENT("Test peek...");
        Object* num2 = MAKE_NUM(10);
        Object* num3 = MAKE_NUM(20);
        vm_push(&sk.vm, num2);
        vm_push(&sk.vm, num3);
        
        Object* peeked = vm_peek(&sk.vm, 0);
        TEST(peeked == num3);
        TEST(sk.vm.sp == 2);
        
        peeked = vm_peek(&sk.vm, 1);
        TEST(peeked == num2);
        
        COMMENT("Test pop_n...");
        vm_pop_n(&sk.vm, 2);
        TEST(sk.vm.sp == 0);
        
        COMMENT("Test clear_stack...");
        vm_push(&sk.vm, num1);
        vm_push(&sk.vm, num2);
        vm_push(&sk.vm, num3);
        TEST(sk.vm.sp == 3);
        
        vm_clear_stack(&sk.vm);
        TEST(sk.vm.sp == 0);
        
        vm_shutdown(&sk.vm);
    }
    
    // Test object creation
    SUITE("Object Creation");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        COMMENT("Test number creation...");
        Object* num = MAKE_NUM(123);
        TEST(IS_NUM(num));
        TEST(AS_NUM(num) == 123);
        TEST(!IS_OBJ(num));
        
        COMMENT("Test boolean creation...");
        Object* t = MAKE_TRUE();
        Object* f = MAKE_FALSE();
        TEST(IS_TRUE(t));
        TEST(IS_FALSE(f));
        TEST(IS_BOOL(t));
        TEST(IS_BOOL(f));
        TEST(!IS_OBJ(t));
        TEST(!IS_OBJ(f));
        
        COMMENT("Test string creation...");
        Object* str = vm_new_string(&sk.vm, "hello");
        TEST(str != NULL);
        TEST(IS_OBJ(str));
        TEST(IS_STRING(str));
        TEST(strcmp(((String*)str)->value, "hello") == 0);
        
        vm_shutdown(&sk.vm);
    }
    
    // Test tagged values
    SUITE("Tagged Value System");
    {
        COMMENT("Test number tagging...");
        Object* num = MAKE_NUM(42);
        TEST(IS_NUM(num));
        TEST(!IS_OBJ(num));
        TEST(!IS_BOOL(num));
        TEST(AS_NUM(num) == 42);
        
        COMMENT("Test boolean tagging...");
        Object* t = MAKE_TRUE();
        Object* f = MAKE_FALSE();
        TEST(IS_BOOL(t));
        TEST(IS_BOOL(f));
        TEST(!IS_NUM(t));
        TEST(!IS_OBJ(t));
        TEST(IS_TRUE(t));
        TEST(IS_FALSE(f));
        TEST(!IS_TRUE(f));
        TEST(!IS_FALSE(t));
        
        COMMENT("Test nil...");
        Object* nil = NULL;
        TEST(IS_NIL(nil));
        TEST(!IS_OBJ(nil));
        TEST(!IS_NUM(nil));
    }
    
    // Test return stack
    SUITE("Return Stack");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        VMCode_t code[] = { OP_HALT };
        VMCode_t* addr = &code[0];
        
        vm_push_ip(&sk.vm, addr);
        TEST(sk.vm.return_sp == 1);
        
        VMCode_t* popped_addr = vm_pop_ip(&sk.vm);
        TEST(popped_addr == addr);
        TEST(sk.vm.return_sp == 0);
        
        vm_shutdown(&sk.vm);
    }
    
    // Test global variables
    SUITE("Global Variables");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        Object* key = vm_new_string(&sk.vm, "x");
        Object* val = MAKE_NUM(42);
        
        COMMENT("Test define global...");
        TEST(vm_define_global(&sk.vm, key, val) == VM_OK);
        
        COMMENT("Test get global...");
        Object* retrieved = vm_get_global(&sk.vm, key);
        TEST(retrieved == val);
        TEST(AS_NUM(retrieved) == 42);
        
        COMMENT("Test update global...");
        Object* new_val = MAKE_NUM(100);
        TEST(vm_update_global(&sk.vm, key, new_val) == VM_OK);
        retrieved = vm_get_global(&sk.vm, key);
        TEST(AS_NUM(retrieved) == 100);
        
        vm_shutdown(&sk.vm);
    }
    
    // Test code chunk operations
    SUITE("Code Chunk");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        vm_init(&sk.vm);
        
        CodeChunk chunk;
        memset(&chunk, 0, sizeof(CodeChunk));
        vm_init_chunk(&chunk);
        
        TEST(chunk.count == 0);
        TEST(chunk.capacity == 0);  // Capacity starts at 0, grows on demand
        TEST(chunk.code == NULL);
        
        vm_free_chunk(&sk.vm, &chunk);
        
        vm_shutdown(&sk.vm);
    }
}
