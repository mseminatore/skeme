# Unified Error Reporting Infrastructure

## Overview

The unified error reporting infrastructure provides a consistent way to handle errors, warnings, and other log messages throughout the Skeme interpreter. It replaces the fragmented error reporting mechanisms with a single, extensible system.

## Key Features

- **Unified Logging**: All error messages route through `sk_log()`
- **Custom Error Handlers**: Applications can implement custom error handling
- **Error Redirection**: Errors can be redirected to files or buffers
- **Log Level Filtering**: Support for ERROR, WARNING, INFO, and DEBUG levels
- **File/Line Information**: Automatic file and line context from parser state
- **Color Output**: Terminal color support maintained
- **Output Redirection**: `display` and `newline` can be redirected to custom streams

## API Reference

### Log Levels

```c
typedef enum {
    SK_LOG_ERROR,    // Critical errors
    SK_LOG_WARNING,  // Warnings
    SK_LOG_INFO,     // Informational messages
    SK_LOG_DEBUG     // Debug output
} SKLogLevel;
```

### Error Handler Type

```c
typedef void (*SKErrorHandler)(SKState* sk, SKLogLevel level, 
                                const char* file, int line, const char* msg);
```

### Main Functions

#### sk_log()
```c
void sk_log(SKState* sk, SKLogLevel level, const char* fmt, ...);
```

Logs a message at the specified level. Supports printf-style format strings.

**Example:**
```c
sk_log(sk, SK_LOG_ERROR, "Invalid argument: %d", value);
sk_log(sk, SK_LOG_WARNING, "Deprecated function called");
sk_log(sk, SK_LOG_INFO, "Processing file: %s", filename);
```

#### sk_set_error_handler()
```c
void sk_set_error_handler(SKState* sk, SKErrorHandler handler);
```

Sets a custom error handler. Pass NULL to use the default handler.

**Example:**
```c
void my_error_handler(SKState* sk, SKLogLevel level,
                      const char* file, int line, const char* msg)
{
    if (level == SK_LOG_ERROR) {
        fprintf(my_log_file, "[ERROR] %s:%d - %s\n", file, line, msg);
    }
}

sk_set_error_handler(sk, my_error_handler);
```

#### sk_default_error_handler()
```c
void sk_default_error_handler(SKState* sk, SKLogLevel level,
                               const char* file, int line, const char* msg);
```

The default error handler that writes to `error_stream` with color support.

## SKState Fields

The `SKState` structure now includes:

```c
struct SKState {
    // ... existing fields
    SKErrorHandler error_handler;  // Custom error handler (default: sk_default_error_handler)
    FILE* error_stream;            // Where errors are written (default: stderr)
    FILE* output_stream;           // Where output goes (default: stdout)
};
```

## Usage Examples

### Example 1: Capture Errors in a Buffer

```c
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "error.h"

char error_buffer[4096];
int error_count = 0;

void buffer_error_handler(SKState* sk, SKLogLevel level,
                          const char* file, int line, const char* msg)
{
    if (level == SK_LOG_ERROR) {
        size_t len = strlen(error_buffer);
        snprintf(error_buffer + len, sizeof(error_buffer) - len,
                 "Error in %s:%d - %s\n", file ? file : "unknown", line, msg);
        error_count++;
    }
}

int main() {
    SKState sk;
    sk_init(&sk, 0, NULL);
    
    // Install custom handler
    sk_set_error_handler(&sk, buffer_error_handler);
    
    // Parse some code...
    sk_parse(&sk);
    
    // Check for errors
    if (error_count > 0) {
        printf("Errors occurred:\n%s", error_buffer);
    }
    
    sk_shutdown(&sk);
    return 0;
}
```

### Example 2: Redirect Output to File

```c
#include <stdio.h>
#include "parser.h"

int main() {
    SKState sk;
    sk_init(&sk, 0, NULL);
    
    // Redirect output to file
    FILE* output_file = fopen("output.txt", "w");
    sk.output_stream = output_file;
    
    // Redirect errors to separate file
    FILE* error_file = fopen("errors.txt", "w");
    sk.error_stream = error_file;
    
    // Run interpreter...
    sk_parse(&sk);
    
    // Cleanup
    fclose(output_file);
    fclose(error_file);
    sk_shutdown(&sk);
    return 0;
}
```

### Example 3: Count Errors and Warnings

```c
#include <stdio.h>
#include "parser.h"
#include "error.h"

int main() {
    SKState sk;
    sk_init(&sk, 0, NULL);
    
    // Parse code...
    sk_parse(&sk);
    
    // Check error counts (automatically updated by sk_log)
    printf("Errors: %d\n", sk.err_count);
    printf("Warnings: %d\n", sk.warn_count);
    
    sk_shutdown(&sk);
    return 0;
}
```

## Migration Guide

### Before (Old Code)

```c
// Old error reporting
fprintf(stderr, "Error: invalid syntax\n");
yyerror("parse error");
LOG("Debug: value = %d\n", x);
```

### After (New Code)

```c
// New unified error reporting
sk_log(sk, SK_LOG_ERROR, "invalid syntax");
sk_log(sk, SK_LOG_ERROR, "parse error");  // yyerror internally uses this
sk_log(sk, SK_LOG_DEBUG, "value = %d", x);
```

## Backward Compatibility

The following functions still work but now use the unified system internally:

- `yyerror()` - Routes to `sk_log(SK_LOG_ERROR)`
- `yywarning()` - Routes to `sk_log(SK_LOG_WARNING)`
- `yyinfo()` - Routes to `sk_log(SK_LOG_INFO)`
- `LOG()` - Maintained for debug output (checks g_bDebug flag)
- `VM_LOG()` - Maintained for VM debug output (checks g_bVMLog flag)
- `GC_LOG()` - Maintained for GC debug output (checks g_bGCLog flag)

## Testing

Comprehensive tests are available in `tests/test_error.c`:

- Custom error handler testing
- Error/warning counting
- File and line information
- Variadic message formatting
- NULL safety

Run tests with:
```bash
./build/skeme_tests
```

## Demo

A demonstration script `error_demo.scm` shows output redirection:

```bash
./build/skeme error_demo.scm
```

## Benefits

1. **Consistency**: All errors formatted the same way
2. **Flexibility**: Custom handlers enable integration with external systems
3. **Testability**: Errors can be captured programmatically in tests
4. **Maintainability**: Single point of control for all error output
5. **Extensibility**: Easy to add new log levels or features
6. **Debugging**: File/line information automatically included

## Future Enhancements

Potential future improvements:

- Log level filtering (suppress debug in release builds)
- Structured logging (JSON output)
- Log rotation
- Network logging
- Performance metrics
- Stack traces
