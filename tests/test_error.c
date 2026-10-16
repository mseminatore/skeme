//--------------------------------------------------------------
// test_error.c - Tests for unified error reporting infrastructure
//--------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "../hash/testy/test.h"
#include "../error.h"
#include "../sk.h"

// Test context for custom handler
typedef struct {
    int error_count;
    int warning_count;
    int info_count;
    int debug_count;
    char last_message[512];
    const char* last_file;
    int last_line;
} ErrorTestContext;

static ErrorTestContext g_test_ctx;

//--------------------------------------------------------------
// Custom error handler for testing
//--------------------------------------------------------------
static void test_error_handler(SKState* sk, SKLogLevel level,
                                const char* file, int line, const char* msg)
{
    switch (level) {
        case SK_LOG_ERROR:
            g_test_ctx.error_count++;
            break;
        case SK_LOG_WARNING:
            g_test_ctx.warning_count++;
            break;
        case SK_LOG_INFO:
            g_test_ctx.info_count++;
            break;
        case SK_LOG_DEBUG:
            g_test_ctx.debug_count++;
            break;
    }
    
    strncpy(g_test_ctx.last_message, msg, sizeof(g_test_ctx.last_message) - 1);
    g_test_ctx.last_file = file;
    g_test_ctx.last_line = line;
}

//--------------------------------------------------------------
// Reset test context
//--------------------------------------------------------------
static void reset_test_context(void)
{
    memset(&g_test_ctx, 0, sizeof(ErrorTestContext));
}

//--------------------------------------------------------------
// Compile and run code, then drop anything it left on the stack
//--------------------------------------------------------------
static void eval_quietly(SKState* sk, const char* code)
{
    sk_eval_string(sk, code);
    vm_clear_stack(&sk->vm);
}

//--------------------------------------------------------------
// The value of a global variable (NULL if there is no such global)
//--------------------------------------------------------------
static Object* get_global(SKState* sk, const char* name)
{
    return vm_get_global(&sk->vm, vm_new_string(&sk->vm, name));
}

//--------------------------------------------------------------
// Is a global missing, or only a placeholder for a name that was used?
//--------------------------------------------------------------
static int no_value(Object* value)
{
    return value == NULL || IS_UNBOUND(value);
}

//--------------------------------------------------------------
// Run all error tests
//--------------------------------------------------------------
void test_error(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    MODULE("Error Reporting");
    
    // Test: sk_log with custom handler
    SUITE("Custom Error Handler");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk.error_handler = sk_default_error_handler;
        sk.error_stream = stderr;
        sk.output_stream = stdout;
        
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);
        
        COMMENT("Test error logging...");
        sk_log(&sk, SK_LOG_ERROR, "Test error message");
        TEST(g_test_ctx.error_count == 1);
        TEST(strcmp(g_test_ctx.last_message, "Test error message") == 0);
        TEST(sk.err_count == 1);
        
        COMMENT("Test warning logging...");
        sk_log(&sk, SK_LOG_WARNING, "Test warning message");
        TEST(g_test_ctx.warning_count == 1);
        TEST(strcmp(g_test_ctx.last_message, "Test warning message") == 0);
        TEST(sk.warn_count == 1);
        
        COMMENT("Test info logging...");
        sk_log(&sk, SK_LOG_INFO, "Test info message");
        TEST(g_test_ctx.info_count == 1);
        TEST(strcmp(g_test_ctx.last_message, "Test info message") == 0);
        
        COMMENT("Test debug logging...");
        sk_log(&sk, SK_LOG_DEBUG, "Test debug message");
        TEST(g_test_ctx.debug_count == 1);
        TEST(strcmp(g_test_ctx.last_message, "Test debug message") == 0);
    }
    
    // Test: sk_log with file/line information
    SUITE("Error with File Info");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        
        // Setup file stack
        sk.file_stack_capacity = 10;
        sk.file_stack = (FileNode*)calloc(sk.file_stack_capacity, sizeof(FileNode));
        sk.file_stack_ptr = 0;
        sk.file_stack[0].filename = "test.scm";
        sk.file_stack[0].yylineno = 42;
        
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);
        
        COMMENT("Test file and line info...");
        sk_log(&sk, SK_LOG_ERROR, "Error at line 42");
        
        TEST(g_test_ctx.last_file != NULL);
        TEST(strcmp(g_test_ctx.last_file, "test.scm") == 0);
        TEST(g_test_ctx.last_line == 42);
        
        free(sk.file_stack);
    }
    
    // Test: Error counter increments
    SUITE("Error Counting");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk.error_handler = sk_default_error_handler;
        sk.error_stream = stderr;
        sk.err_count = 0;
        sk.warn_count = 0;
        
        COMMENT("Log multiple errors...");
        sk_log(&sk, SK_LOG_ERROR, "Error 1");
        sk_log(&sk, SK_LOG_ERROR, "Error 2");
        sk_log(&sk, SK_LOG_ERROR, "Error 3");
        
        TEST(sk.err_count == 3);
        
        COMMENT("Log multiple warnings...");
        sk_log(&sk, SK_LOG_WARNING, "Warning 1");
        sk_log(&sk, SK_LOG_WARNING, "Warning 2");
        TEST(sk.warn_count == 2);
        
        COMMENT("Info/debug should not increment counters...");
        sk_log(&sk, SK_LOG_INFO, "Info");
        sk_log(&sk, SK_LOG_DEBUG, "Debug");
        TEST(sk.err_count == 3);
        TEST(sk.warn_count == 2);
    }
    
    // Test: Variadic format strings
    SUITE("Variadic Messages");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);
        
        COMMENT("Test formatted messages...");
        sk_log(&sk, SK_LOG_ERROR, "Error code: %d", 42);
        TEST(strcmp(g_test_ctx.last_message, "Error code: 42") == 0);
        
        sk_log(&sk, SK_LOG_WARNING, "Value: %s = %d", "x", 100);
        TEST(strcmp(g_test_ctx.last_message, "Value: x = 100") == 0);
    }
    
    // Test: NULL safety
    SUITE("NULL Safety");
    {
        COMMENT("Should not crash with NULL sk...");
        sk_log(NULL, SK_LOG_ERROR, "Should not crash");
        TEST(1); // If we get here, we didn't crash
        
        COMMENT("Should not crash with NULL fmt...");
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_log(&sk, SK_LOG_ERROR, NULL);
        TEST(1); // If we get here, we didn't crash
        
        COMMENT("Set NULL handler should not crash...");
        sk_set_error_handler(NULL, test_error_handler);
        TEST(1); // Should not crash
    }

    // Test: a syntax error drops only the bad top-level form
    SUITE("Parse Error Recovery");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        COMMENT("Forms after a syntax error still run, the bad form does not...");
        eval_quietly(&sk, "(define pe-a 1) (define pe-b (car '(. 2))) (define pe-c 3)");
        TEST(g_test_ctx.error_count >= 1);
        TEST(AS_NUM(get_global(&sk, "pe-a")) == 1);
        TEST(AS_NUM(get_global(&sk, "pe-c")) == 3);
        TEST(IS_UNBOUND(get_global(&sk, "pe-b")));    // its define never ran
        TEST(sk.vm.sp == 0);

        COMMENT("An unclosed form at the end of the input is reported, not looped on...");
        reset_test_context();
        eval_quietly(&sk, "(define pe-d 4) (define (pe-f) (display 1)");
        TEST(g_test_ctx.error_count >= 1);
        TEST(AS_NUM(get_global(&sk, "pe-d")) == 4);

        sk_shutdown(&sk);
    }

    // Test: running out of stack is a runtime error, not an abort
    SUITE("Stack Overflow");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        eval_quietly(&sk, "(define (so-deep n) (if (= n 0) 0 (+ 1 (so-deep (- n 1)))))");
        TEST(g_test_ctx.error_count == 0);

        COMMENT("Deep but bounded recursion fits the default stack...");
        eval_quietly(&sk, "(define so-ok (so-deep 10000))");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "so-ok")) == 10000);

        COMMENT("Unbounded recursion reports stack overflow and unwinds...");
        eval_quietly(&sk, "(define so-bad (so-deep 10000000))");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "stack overflow") != NULL);
        TEST(sk.vm.sp == 0);
        TEST(sk.vm.fp == 0);
        TEST(sk.vm.abort_exec == 0);

        COMMENT("The VM keeps working afterwards...");
        eval_quietly(&sk, "(define so-after (so-deep 100))");
        TEST(AS_NUM(get_global(&sk, "so-after")) == 100);

        COMMENT("Overflow inside a native callback (map) unwinds too...");
        reset_test_context();
        eval_quietly(&sk, "(define so-map (map (lambda (x) (so-deep 10000000)) (list 1 2 3)))");
        TEST(g_test_ctx.error_count == 1);
        TEST(sk.vm.sp == 0);

        sk_shutdown(&sk);
    }

    // Test: globals are looked up by name when the code runs
    SUITE("Unbound Variables");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        COMMENT("A forward reference compiles without error...");
        eval_quietly(&sk, "(define (ub-g) (ub-h 1))");
        TEST(g_test_ctx.error_count == 0);

        COMMENT("Calling it before the definition exists is an unbound variable error...");
        eval_quietly(&sk, "(define ub-r1 (ub-g))");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "unbound variable 'ub-h'") != NULL);
        TEST(sk.vm.sp == 0);

        COMMENT("Once defined, the same code finds it...");
        eval_quietly(&sk, "(define (ub-h x) (+ x 41)) (define ub-r2 (ub-g))");
        TEST(AS_NUM(get_global(&sk, "ub-r2")) == 42);

        sk_shutdown(&sk);
    }

    // Test: errors from calls: arity, stale continuations, native nesting
    SUITE("Call Errors");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        COMMENT("A wrong argument count names the procedure and aborts...");
        eval_quietly(&sk, "(define (ce-two a b) a) (define ce-r (ce-two 1))");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "'ce-two' called with 1 argument, expects 2") != NULL);
        TEST(IS_UNBOUND(get_global(&sk, "ce-r")));
        TEST(sk.vm.sp == 0);

        COMMENT("Too few arguments for a variadic procedure...");
        reset_test_context();
        eval_quietly(&sk, "(define (ce-v a . r) a) (ce-v)");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "expects at least 1") != NULL);

        COMMENT("Calling a continuation after its call/cc returned is an error...");
        reset_test_context();
        eval_quietly(&sk, "(define ce-k #f) (define ce-x (call/cc (lambda (k) (set! ce-k k) 1)))");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "ce-x")) == 1);
        eval_quietly(&sk, "(ce-k 2)");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "only escaping continuations") != NULL);
        TEST(sk.vm.sp == 0);
        TEST(sk.vm.escape_to == NULL);

        COMMENT("Deep recursion through native callbacks stops with an error, not a crash...");
        reset_test_context();
        eval_quietly(&sk, "(define (ce-deep n) (if (= n 0) 0 (+ 1 (call/cc (lambda (k) (ce-deep (- n 1)))))))");
        eval_quietly(&sk, "(define ce-ok (ce-deep 100))");
        TEST(AS_NUM(get_global(&sk, "ce-ok")) == 100);
        eval_quietly(&sk, "(define ce-bad (ce-deep 100000))");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "nested too deeply") != NULL);
        TEST(sk.vm.sp == 0);
        TEST(sk.vm.exec_depth == 0);

        COMMENT("The VM keeps working afterwards...");
        eval_quietly(&sk, "(define ce-after (ce-deep 10))");
        TEST(AS_NUM(get_global(&sk, "ce-after")) == 10);

        sk_shutdown(&sk);
    }

    // Test: define inside a procedure must come at the start of a body
    SUITE("Define Placement");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        COMMENT("Internal defines at the start of a body are local...");
        eval_quietly(&sk, "(define (dp-ok) (define dp-x 1) (define (dp-f) dp-x) (dp-f)) (define dp-r (dp-ok))");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "dp-r")) == 1);
        TEST(no_value(get_global(&sk, "dp-x")));
        TEST(no_value(get_global(&sk, "dp-f")));

        COMMENT("A define after a body expression is an error...");
        reset_test_context();
        eval_quietly(&sk, "(define (dp-late) (display \"\") (define dp-y 2) dp-y)");
        TEST(g_test_ctx.error_count == 1);
        TEST(strstr(g_test_ctx.last_message, "start of a body") != NULL);
        TEST(no_value(get_global(&sk, "dp-late")));
        TEST(no_value(get_global(&sk, "dp-y")));

        COMMENT("A dropped redefinition keeps the old procedure...");
        reset_test_context();
        eval_quietly(&sk, "(define (dp-keep) 1)");
        eval_quietly(&sk, "(define (dp-keep) (display \"\") (define dp-z 2) 3)");
        TEST(g_test_ctx.error_count == 1);
        eval_quietly(&sk, "(define dp-kept (dp-keep))");
        TEST(g_test_ctx.error_count == 1);
        TEST(AS_NUM(get_global(&sk, "dp-kept")) == 1);

        COMMENT("So is a define nested in an expression...");
        reset_test_context();
        eval_quietly(&sk, "(define (dp-if c) (if c (define (dp-g) 1)) 0)");
        TEST(g_test_ctx.error_count == 1);
        TEST(no_value(get_global(&sk, "dp-g")));

        COMMENT("Parsing continues after the error...");
        reset_test_context();
        eval_quietly(&sk, "(define dp-after 5)");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "dp-after")) == 5);

        sk_shutdown(&sk);
    }

    // Test: reading code from a string (sk_eval_string, eval)
    SUITE("Eval Input");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        COMMENT("A symbol or number at the very end of the string is read once...");
        eval_quietly(&sk, "(define ei-x 5)");
        eval_quietly(&sk, "(define ei-a 0) (set! ei-a ei-x)");
        TEST(AS_NUM(get_global(&sk, "ei-a")) == 5);
        sk_eval_string(&sk, "ei-x");
        TEST(sk.vm.sp == 1 && AS_NUM(sk.vm.stack[0]) == 5);
        vm_clear_stack(&sk.vm);
        sk_eval_string(&sk, "42");
        TEST(sk.vm.sp == 1 && AS_NUM(sk.vm.stack[0]) == 42);
        vm_clear_stack(&sk.vm);
        TEST(g_test_ctx.error_count == 0);

        COMMENT("load inside a string reads the file, then the rest of the string...");
        FILE* f = fopen("ei_load_test.scm", "w");
        TEST(f != NULL);
        if (f)
        {
            fputs("(define ei-loaded 7)\n(define ei-loaded2 (+ ei-loaded 1))", f);
            fclose(f);
        }
        int depth = sk.file_stack_ptr;
        eval_quietly(&sk, "(load \"ei_load_test.scm\") (define ei-after (+ ei-loaded2 1))");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "ei-loaded")) == 7);
        TEST(AS_NUM(get_global(&sk, "ei-after")) == 9);
        TEST(sk.file_stack_ptr == depth);

        COMMENT("eval of a string that loads a file...");
        eval_quietly(&sk, "(define ei-loaded 0) (eval \"(load \\\"ei_load_test.scm\\\")\") (define ei-again ei-loaded)");
        TEST(g_test_ctx.error_count == 0);
        TEST(AS_NUM(get_global(&sk, "ei-again")) == 7);
        TEST(sk.file_stack_ptr == depth);
        remove("ei_load_test.scm");

        sk_shutdown(&sk);
    }

    // Test: natives with no result (display, newline) still yield a value
    SUITE("Unspecified Results");
    {
        SKState sk;
        memset(&sk, 0, sizeof(SKState));
        sk_init(&sk, 0, NULL);
        reset_test_context();
        sk_set_error_handler(&sk, test_error_handler);

        eval_quietly(&sk, "(define us-v (display \"\")) (define us-l (list (display \"\") 2))");
        TEST(g_test_ctx.error_count == 0);
        TEST(IS_UNSPEC(get_global(&sk, "us-v")));
        Object* l = get_global(&sk, "us-l");
        TEST(IS_PAIR(l) && IS_UNSPEC(((Pair*)l)->car));
        TEST(sk.vm.sp == 0);

        sk_shutdown(&sk);
    }
}
