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

    if (sk_reading_string(sk) && sk->eval_name) {
        // code from a string (sk_eval_string): its name and the line in it
        file = sk->eval_name;
        line = sk->eval_lineno;
    } else if (sk->file_stack_ptr >= 0 && sk->file_stack) {
        file = sk->file_stack[sk->file_stack_ptr].filename;
        line = sk->file_stack[sk->file_stack_ptr].yylineno;
    }

    // Call the error handler
    if (sk->error_handler) {
        sk->error_handler(sk, level, file, line, msg);
    }

    // Update error/warning counts
    if (level == SK_LOG_ERROR) {
        sk->err_count++;
    } else if (level == SK_LOG_WARNING) {
        sk->warn_count++;
    }
}
