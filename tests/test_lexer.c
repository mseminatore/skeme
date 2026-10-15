//------------------------------------------------------
// Skeme Unit Tests - Lexer Tests
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../lexer.h"
#include "../sk.h"

void test_lexer(int argc, char *argv[]) {
    MODULE("Lexer");
    
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

    // Test YYLVAL word buffer
    SUITE("YYLVAL Buffer");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);

        COMMENT("Test yylval word buffer size...");
        TEST(sizeof(sk.yylval.word) == BUF_SIZE);

        sk_shutdown(&sk);
    }
}
