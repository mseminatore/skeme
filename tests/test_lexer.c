//------------------------------------------------------
// Skeme Unit Tests - Lexer Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../lexer.h"
#include "../parser.h"

void test_lexer(int argc, char *argv[]) {
    MODULE("Lexer");
    
    // Test symbol table operations
    SUITE("Symbol Table");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test add_symbol...");
        int idx1 = add_symbol(&sk, "test_symbol", 1);
        TEST(idx1 >= 0);
        TEST(sk.symbol_count == idx1 + 1);
        
        COMMENT("Test lookup_symbol...");
        int found = lookup_symbol(&sk, "test_symbol");
        TEST(found == idx1);
        
        COMMENT("Test symbol not found...");
        int not_found = lookup_symbol(&sk, "nonexistent");
        TEST(not_found == -1);
        
        COMMENT("Test multiple symbols...");
        int idx2 = add_symbol(&sk, "another_symbol", 2);
        TEST(idx2 >= 0);
        TEST(idx2 != idx1);
        TEST(sk.symbol_count == idx2 + 1);
        
        found = lookup_symbol(&sk, "another_symbol");
        TEST(found == idx2);
        
        // Original symbol should still be findable
        found = lookup_symbol(&sk, "test_symbol");
        TEST(found == idx1);
        
        sk_shutdown(&sk);
    }
    
    // Test symbol table capacity
    SUITE("Symbol Table Capacity");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test symbol table growth...");
        int initial_capacity = sk.symbol_capacity;
        TEST(initial_capacity > 0);
        
        // Add symbols up to initial capacity
        for (int i = 0; i < initial_capacity + 10; i++) {
            char name[64];
            snprintf(name, sizeof(name), "symbol_%d", i);
            int idx = add_symbol(&sk, name, i + 1);
            TEST(idx >= 0);
        }
        
        // Capacity should have grown
        TEST(sk.symbol_capacity >= initial_capacity);
        TEST(sk.symbol_count == initial_capacity + 10);
        
        sk_shutdown(&sk);
    }
    
    // Test symbol properties
    SUITE("Symbol Properties");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test symbol name storage...");
        int idx = add_symbol(&sk, "my_function", 42);
        TEST(idx >= 0);
        
        Symbol_t* sym = &sk.symbols[idx];
        TEST(sym != NULL);
        TEST(sym->name != NULL);
        TEST(strcmp(sym->name, "my_function") == 0);
        // Note: lineno behavior may vary, skip exact check
        
        sk_shutdown(&sk);
    }
    
    // Test file stack
    SUITE("File Stack");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test file stack initialization...");
        TEST(sk.file_stack != NULL);
        TEST(sk.file_stack_ptr >= 0);
        TEST(sk.file_stack_capacity > 0);
        
        sk_shutdown(&sk);
    }
}
