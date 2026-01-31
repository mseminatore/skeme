//--------------------------------------------------------------
// error.h - Unified error reporting infrastructure
//--------------------------------------------------------------
#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

// Forward declaration
typedef struct SKState SKState;

//--------------------------------------------------------------
// Log levels for unified error reporting
//--------------------------------------------------------------
typedef enum {
    SK_LOG_ERROR,
    SK_LOG_WARNING,
    SK_LOG_INFO,
    SK_LOG_DEBUG
} SKLogLevel;

//--------------------------------------------------------------
// Error handler function pointer type
// Parameters:
//   sk    - SKState instance
//   level - Log level
//   file  - Source file (can be NULL)
//   line  - Line number (0 if not applicable)
//   msg   - Formatted message
//--------------------------------------------------------------
typedef void (*SKErrorHandler)(SKState* sk, SKLogLevel level, 
                                const char* file, int line, const char* msg);

//--------------------------------------------------------------
// Unified logging function
// Routes all error/warning/info/debug messages through the
// error handler
//--------------------------------------------------------------
void sk_log(SKState* sk, SKLogLevel level, const char* fmt, ...);

//--------------------------------------------------------------
// Set custom error handler
// If handler is NULL, uses default handler
//--------------------------------------------------------------
void sk_set_error_handler(SKState* sk, SKErrorHandler handler);

//--------------------------------------------------------------
// Default error handler
// Writes to error_stream with color support
//--------------------------------------------------------------
void sk_default_error_handler(SKState* sk, SKLogLevel level,
                               const char* file, int line, const char* msg);

#endif // ERROR_H
