//--------------------------------------------------------------
// error.c - Unified error reporting infrastructure
//--------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "error.h"
#include "parser.h"
#include "term_colors.h"

#define ERROR_BUF_SIZE 2048

//--------------------------------------------------------------
// Default error handler
// Writes to error_stream with color-coded output
//--------------------------------------------------------------
void sk_default_error_handler(SKState* sk, SKLogLevel level,
                               const char* file, int line, const char* msg)
{
    FILE* stream = sk ? sk->error_stream : stderr;
    if (!stream)
        stream = stderr;

    const char* color = "";
    const char* reset = TERM_RESET;
    const char* level_str = "";

    switch (level) {
        case SK_LOG_ERROR:
            color = TERM_RED;
            level_str = "ERROR";
            break;
        case SK_LOG_WARNING:
            color = TERM_YELLOW;
            level_str = "WARNING";
            break;
        case SK_LOG_INFO:
            color = TERM_GREEN;
            level_str = "INFO";
            break;
        case SK_LOG_DEBUG:
            color = "";
            reset = "";
            level_str = "DEBUG";
            break;
    }

    if (file && line > 0) {
        fprintf(stream, "%s%s: '%s' in file '%s' near line %d%s\n",
                color, level_str, msg, file, line, reset);
    } else if (level == SK_LOG_DEBUG) {
        // Debug output without level prefix for compatibility
        fprintf(stream, "%s", msg);
    } else {
        fprintf(stream, "%s%s: '%s'%s\n", color, level_str, msg, reset);
    }
}

//--------------------------------------------------------------
// Set custom error handler
//--------------------------------------------------------------
void sk_set_error_handler(SKState* sk, SKErrorHandler handler)
{
    if (!sk)
        return;

    sk->error_handler = handler ? handler : sk_default_error_handler;
}

//--------------------------------------------------------------
// Unified logging function
//--------------------------------------------------------------
void sk_log(SKState* sk, SKLogLevel level, const char* fmt, ...)
{
    if (!sk || !fmt)
        return;

    char msg[ERROR_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, ERROR_BUF_SIZE, fmt, args);
    va_end(args);

    // Get file and line info from file_stack if available
    const char* file = NULL;
    int line = 0;

    int runtime = (sk->parse_depth == 0 && sk->vm.exec_depth > 0);

    // A runtime error while an exception handler is installed is raised,
    // as an error object, instead of reported: a guard can catch it. If no
    // handler takes it, vm_raise reports it after all.
    if (runtime && level == SK_LOG_ERROR && sk->vm.handlers && !sk->vm.uncatchable && !sk->vm.abort_exec) {
        Object* message = vm_new_string(&sk->vm, msg);
        Object* err = vm_new_error(&sk->vm, message, NULL);
        vm_raise(&sk->vm, err, 0);
        if (!sk->vm.abort_exec)
            vm_stop(&sk->vm);       // (vm_raise always unwinds; this is a backstop)
        return;
    }

    if (runtime) {
        // a runtime error: where the running instruction was compiled from
        if (!vm_current_location(&sk->vm, &file, &line))
            file = NULL;
    } else if (!sk_source_location(sk, &file, &line)) {
        // a parse error: where the lexer is (a file, or <eval> for a string)
        file = NULL;
    }

    // the REPL's input needs no location
    if (file && strcmp(file, "stdin") == 0)
        file = NULL;

    // Call the error handler
    if (sk->error_handler) {
        sk->error_handler(sk, level, file, line, msg);
    }

    // Update error/warning counts
    if (level == SK_LOG_ERROR) {
        sk->err_count++;

        // a runtime error stops the run, so a native that reports one does
        // not go on with a made-up result (vm_abort does this for the VM's own)
        if (runtime)
            vm_stop(&sk->vm);
    } else if (level == SK_LOG_WARNING) {
        sk->warn_count++;
    }
}
