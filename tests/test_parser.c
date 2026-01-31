//------------------------------------------------------
// Skeme Unit Tests - Parser Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../parser.h"
#include "../vm.h"

void test_parser(int argc, char *argv[]) {
    MODULE("Parser");
    
    // Test SKState initialization
    SUITE("SKState Initialization");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        
        int result = sk_init(&sk, 0, NULL);
        // sk_init returns SK_TRUE (non-zero) on success
        TEST(result != 0);
        TEST(sk.symbols != NULL);
        TEST(sk.symbol_count >= 0);
        TEST(sk.symbol_capacity > 0);
        TEST(sk.file_stack != NULL);
        TEST(sk.file_stack_capacity > 0);
        TEST(sk.err_count == 0);
        TEST(sk.warn_count == 0);
        
        sk_shutdown(&sk);
        TEST(sk.symbols == NULL);
        TEST(sk.file_stack == NULL);
    }
    
    // Test error counting
    SUITE("Error Tracking");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test initial error count...");
        TEST(sk.err_count == 0);
        TEST(sk.warn_count == 0);
        
        sk_shutdown(&sk);
    }
    
    // Test parse logging flag
    SUITE("Parse Logging");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        
        COMMENT("Test parse log flag...");
        TEST(sk.parse_log_enabled == 0 || sk.parse_log_enabled == 1);
        
        sk_shutdown(&sk);
    }
}
