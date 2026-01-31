//------------------------------------------------------
// Skeme Unit Tests - Main Test Runner
//
// Copyright 2026 Mark Seminatore. All rights reserved.
//------------------------------------------------------
#include <stdio.h>
#include "../hash/testy/test.h"

// Forward declarations for test functions
void test_vm(int argc, char *argv[]);
void test_gc(int argc, char *argv[]);
void test_lexer(int argc, char *argv[]);
void test_parser(int argc, char *argv[]);
void test_builtins(int argc, char *argv[]);

void test_main(int argc, char *argv[]) {
    // Run VM tests
    test_vm(argc, argv);
    
    // Run GC tests
    test_gc(argc, argv);
    
    // Run lexer tests
    test_lexer(argc, argv);
    
    // Run parser tests
    test_parser(argc, argv);
    
    // Run builtins tests
    test_builtins(argc, argv);
}
