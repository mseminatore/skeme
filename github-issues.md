# GitHub Issues for Skeme Project

---

## Issue #1: Replace Assertions with Proper Error Handling

### Priority
🔴 Critical

### Description
The codebase uses `assert()` statements for critical error conditions that will crash the program in production builds. This prevents graceful error recovery and provides poor user experience when errors occur. Key examples:
- `lexer.c:167` - Symbol table overflow crashes instead of reporting error
- `vm.c:135` - Null chunk pointer crashes
- Fixed-size `symbols[MAX_SYMBOLS]` array has no overflow protection

### Proposed Implementation
1. Create standardized error codes enum in `parser.h`:
   ```c
   typedef enum {
       SK_OK = 0,
       SK_ERR_MEMORY,
       SK_ERR_SYMBOL_TABLE_FULL,
       SK_ERR_FILE_STACK_OVERFLOW,
       SK_ERR_INVALID_ARGUMENT
   } SKError;
   ```

2. Replace `assert()` calls with conditional checks that:
   - Call `yyerror()` with descriptive message
   - Return error codes to callers
   - Allow graceful shutdown

3. Add error recovery paths for:
   - Symbol table overflow → dynamic resizing or clear error message
   - File stack overflow → report max include depth exceeded
   - Null pointer checks → return error instead of crash

4. Audit all `assert()` statements in `*.c` files and categorize as:
   - Critical path → add error handling
   - Debug-only → wrap in `#ifdef _DEBUG`

### Testing & Acceptance Criteria
- [ ] Symbol table can handle 4096+ symbols gracefully (error or resize)
- [ ] File include depth >50 reports clear error instead of crash
- [ ] All VM operations check for null chunks and return errors
- [ ] Run test suite with assertions disabled (release build) - no crashes
- [ ] Fuzzing test: random input doesn't cause crashes, only error messages
- [ ] Memory leak check: error paths properly clean up allocated memory
- [ ] New test cases for each error condition added to `test.scm`

---

## Issue #2: Consolidate Global State into SKState

### Priority
🔴 Critical

### Description
Critical state is scattered across global variables, preventing multiple interpreter instances and complicating testing. The `SKState` struct exists but contains only `int unused`. Key globals that should be consolidated:
- `VM vm` (parser.c) - core interpreter state
- `Symbol_t symbols[MAX_SYMBOLS]` (lexer.c) - symbol table
- `FileNode file_nodes[MAX_FILE_STACK]` (lexer.c) - include stack
- `err_count`, `warn_count` (parser.c) - error tracking

This architecture prevents running multiple interpreters, makes unit testing difficult, and isn't thread-safe.

### Proposed Implementation
1. Expand `SKState` struct in `parser.h`:
   ```c
   struct SKState {
       VM vm;
       Symbol_t* symbols;
       int symbol_count;
       int symbol_capacity;
       FileNode* file_stack;
       int file_stack_ptr;
       int file_stack_capacity;
       int err_count;
       int warn_count;
       int parse_log_enabled;
   };
   ```

2. Update all function signatures to accept `SKState*`:
   - `sk_init(SKState* sk, int argc, char* argv[])`
   - `yylex(SKState* sk)`
   - `sk_parse(SKState* sk)`
   - All builtin functions already take `SKState*` ✓

3. Replace global access patterns:
   - `vm` → `sk->vm`
   - `symbols[i]` → `sk->symbols[i]`
   - `err_count++` → `sk->err_count++`

4. Make symbols/file_stack dynamically allocated:
   - Start with reasonable defaults (256 symbols, 10 file depth)
   - Grow as needed using `GROW_ARRAY` pattern

5. Update `main.c` to pass `&sk` through call chain

### Testing & Acceptance Criteria
- [ ] Can create multiple `SKState` instances independently
- [ ] No global variables remain except `const` lookup tables
- [ ] Two interpreters can run simultaneously without interference
- [ ] All existing tests pass with new architecture
- [ ] Memory leak check: `sk_shutdown()` frees all state
- [ ] Benchmark: performance unchanged (±5%)
- [ ] Code compiles without warnings with `-Wall -Wextra`

---

## Issue #3: Disable GC Stress Testing in Production Builds

### Priority
🟡 High

### Description
`VM_STRESS_GC` is unconditionally defined in `vm.h:13`, causing garbage collection on every allocation. This is a debug feature that should never run in production as it causes severe performance degradation. The fibonacci(30) test likely takes 10-100x longer than necessary.

### Proposed Implementation
1. Change `vm.h` definition:
   ```c
   // Uncomment to enable GC stress testing (collect on every allocation)
   #ifdef _DEBUG
   #   define VM_STRESS_GC
   #endif
   ```

2. Add CMake build types:
   ```cmake
   if(NOT CMAKE_BUILD_TYPE)
     set(CMAKE_BUILD_TYPE Release)
   endif()
   
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
   set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
   ```

3. Update Makefile to support debug/release:
   ```makefile
   DEBUG ?= 0
   ifeq ($(DEBUG), 1)
       CFLAGS += -D_DEBUG -g -O0
   else
       CFLAGS += -DNDEBUG -O2
   endif
   ```

4. Add runtime toggle (keep for debugging):
   ```c
   // in builtins.c
   static int sk_gc_stress(SKState* pSK) {
       g_bGCStress = !g_bGCStress;
       yyinfo(g_bGCStress ? "GC stress enabled" : "GC stress disabled");
       return SK_TRUE;
   }
   ```

### Testing & Acceptance Criteria
- [ ] Default build has `VM_STRESS_GC` disabled
- [ ] Debug build (`cmake -DCMAKE_BUILD_TYPE=Debug`) enables GC stress
- [ ] Release build fibonacci(30) completes in <1 second
- [ ] Debug build fibonacci(30) can still run (slowly) with stress GC
- [ ] New `(gc-stress)` Scheme function toggles at runtime
- [ ] Benchmark: 10-100x speedup on recursive functions
- [ ] All tests pass in both debug and release modes

---

## Issue #4: Implement Lambda Expressions

### Priority
🟡 High

### Description
Lambda expressions are fundamental to Scheme but currently unimplemented (listed in `todo.md`). Without lambdas, the language cannot support higher-order functions, closures, or functional programming patterns that are core to Scheme's design philosophy.

Current state:
- `LAMBDA` token exists in lexer
- No parser support for lambda syntax
- No VM support for closures
- `OBJ_CLOSURE` type defined but unused

### Proposed Implementation
1. Add parser support for lambda expressions:
   ```scheme
   (lambda (x y) (+ x y))
   ((lambda (x) (* x x)) 5)  ; => 25
   ```

2. Create closure objects that capture environment:
   ```c
   typedef struct Closure {
       Object base;
       Procedure* procedure;
       Environment* captured_env;
   } Closure;
   ```

3. Extend VM with closure support:
   - `OP_CLOSURE` opcode to create closure with current environment
   - Modify `OP_CALL` to handle closures vs procedures
   - Implement environment chaining for variable lookup

4. Update variable scoping:
   - Local variables bound in lambda parameters
   - Free variables captured from enclosing scope
   - Closures extend environment lifetime via GC

5. Add `OBJ_CLOSURE` to GC marking in `vm_mark_object()`

### Testing & Acceptance Criteria
- [ ] Simple lambda: `((lambda (x) (* x 2)) 5)` returns 10
- [ ] Closure captures free variables: `(let ((x 10)) (lambda (y) (+ x y)))`
- [ ] Higher-order functions work: `(map (lambda (x) (* x 2)) '(1 2 3))`
- [ ] Nested lambdas: `((lambda (x) ((lambda (y) (+ x y)) 3)) 2)` returns 5
- [ ] Closures with mutation: captured `set!` works correctly
- [ ] GC correctly traces closure environments
- [ ] Anonymous functions in `apply`, `filter`, `map`
- [ ] Add 20+ lambda test cases to `test.scm`
- [ ] Performance: lambda calls within 2x of named function calls

---

## Issue #5: Add C Unit Testing Framework

### Priority
🟡 High

### Description
Currently only integration tests exist (`test.scm` runs full interpreter). No unit tests for C code means:
- Cannot test VM opcodes in isolation
- No GC behavior tests
- Error paths untested
- Refactoring is risky

The `hash/testy/` framework is already available but unused for main codebase.

### Proposed Implementation
1. Create `tests/` directory structure:
   ```
   tests/
     test_vm.c        - VM operations, stack, opcodes
     test_gc.c        - GC marking, sweeping, stress tests
     test_parser.c    - Parser edge cases
     test_lexer.c     - Tokenization, symbol table
     test_builtins.c  - Individual builtin functions
     test_main.c      - Test runner using testy framework
   ```

2. Link with testy framework:
   ```cmake
   add_executable(skeme_tests
       tests/test_vm.c
       tests/test_gc.c
       tests/test_main.c
       vm.c builtins.c io.c lexer.c parser.c
   )
   target_link_libraries(skeme_tests PRIVATE ht testy)
   add_test(NAME unit_tests COMMAND skeme_tests)
   ```

3. Write tests for core functionality:
   - VM stack operations (push, pop, overflow)
   - Opcode execution (OP_NUM, OP_STRING, OP_CALL)
   - GC marking all object types
   - GC collection and free list management
   - Symbol table add/lookup/collision handling
   - Error condition handling

4. Add test coverage reporting:
   ```cmake
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
       target_compile_options(skeme_tests PRIVATE --coverage)
       target_link_options(skeme_tests PRIVATE --coverage)
   endif()
   ```

### Testing & Acceptance Criteria
- [ ] Test suite runs independently of integration tests
- [ ] `ctest` runs both unit and integration tests
- [ ] At least 50 unit tests covering core functionality
- [ ] VM opcode tests: all opcodes have test coverage
- [ ] GC tests: mark/sweep correctness, leak detection
- [ ] Error path tests: null pointers, stack overflow, OOM
- [ ] Symbol table tests: max capacity, collisions, lookup
- [ ] Tests run in <1 second
- [ ] Code coverage >60% of vm.c, parser.c, builtins.c
- [ ] CI runs unit tests on every commit

---

## Issue #6: Standardize Build System (Remove Makefile or CMake)

### Priority
🟡 High

### Description
Project maintains both CMake and Makefile with different configurations:
- Makefile links `-lm -lht`, CMake only links `ht`
- Different flags and targets
- Maintenance burden tracking both
- Inconsistent behavior across platforms

### Proposed Implementation
**Recommendation: Keep CMake, remove Makefile**

Reasons:
- CMake is cross-platform (Windows MSVC, Linux GCC, macOS Clang)
- Better IDE integration (VS Code, CLion, Visual Studio)
- Easier dependency management
- Already configured with testing support
- GitHub Actions uses CMake

Steps:
1. Enhance `CMakeLists.txt`:
   ```cmake
   # Add build options
   option(BUILD_TESTS "Build test suite" ON)
   option(ENABLE_COVERAGE "Enable code coverage" OFF)
   
   # Add install targets
   install(TARGETS skeme DESTINATION bin)
   install(FILES stdlib.scm skunit.scm DESTINATION share/skeme)
   
   # Add math library for all platforms
   target_link_libraries(skeme PRIVATE ht m)
   ```

2. Create `CMakePresets.json` for common configurations:
   ```json
   {
     "version": 3,
     "configurePresets": [
       {"name": "debug", "binaryDir": "build/debug", "cacheVariables": {"CMAKE_BUILD_TYPE": "Debug"}},
       {"name": "release", "binaryDir": "build/release", "cacheVariables": {"CMAKE_BUILD_TYPE": "Release"}}
     ]
   }
   ```

3. Update README.md with CMake-only instructions

4. Add `.vscode/tasks.json` for CMake builds

5. Remove `Makefile` after verification

### Testing & Acceptance Criteria
- [ ] CMake builds successfully on Windows (MSVC)
- [ ] CMake builds successfully on Linux (GCC)
- [ ] CMake builds successfully on macOS (Clang)
- [ ] All tests pass with CMake build
- [ ] `cmake --install` works correctly
- [ ] Debug and release presets work
- [ ] GitHub Actions CI uses CMake exclusively
- [ ] README.md updated with build instructions
- [ ] Makefile removed from repository

---

## Issue #7: Unify Error Reporting Infrastructure

### Priority
🟡 Medium

### Description
Error reporting is inconsistent across the codebase with multiple mechanisms:
- `yyerror()`, `yywarning()`, `yyinfo()` for parser errors
- `LOG()` for debug output (uses `g_bDebug` flag)
- `fprintf(stderr, ...)` direct calls
- `printf()` for output
- VM logging via `VM_LOG()` macro

This makes it impossible to:
- Redirect errors to a file or buffer
- Capture errors programmatically
- Implement custom error handlers
- Format errors consistently

### Proposed Implementation
1. Create error subsystem in new `error.h/c`:
   ```c
   typedef enum {
       SK_LOG_ERROR,
       SK_LOG_WARNING,
       SK_LOG_INFO,
       SK_LOG_DEBUG
   } SKLogLevel;
   
   typedef void (*SKErrorHandler)(SKState* sk, SKLogLevel level, 
                                   const char* file, int line, const char* msg);
   
   void sk_log(SKState* sk, SKLogLevel level, const char* fmt, ...);
   void sk_set_error_handler(SKState* sk, SKErrorHandler handler);
   ```

2. Add error handler to `SKState`:
   ```c
   struct SKState {
       // ... existing fields
       SKErrorHandler error_handler;
       FILE* error_stream;
       FILE* output_stream;
   };
   ```

3. Replace all error reporting:
   - `yyerror(msg)` → `sk_log(sk, SK_LOG_ERROR, msg)`
   - `LOG(fmt, ...)` → `sk_log(sk, SK_LOG_DEBUG, fmt, ...)`
   - Direct `fprintf(stderr)` → `sk_log(sk, SK_LOG_INFO, ...)`

4. Provide default handler that maintains current behavior

5. Update builtins to use output_stream:
   - `sk_display()` writes to `sk->output_stream`
   - `sk_newline()` writes to `sk->output_stream`

### Testing & Acceptance Criteria
- [ ] All error messages route through `sk_log()`
- [ ] Custom error handler can capture all errors
- [ ] Error output can be redirected to file
- [ ] Log level filtering works (e.g., suppress debug in release)
- [ ] File/line information included in error messages
- [ ] Color output still works for terminal
- [ ] Existing test output unchanged
- [ ] Test: redirect errors to buffer and verify contents
- [ ] Test: custom handler counts errors/warnings

---

## Issue #8: Abstract Platform-Specific Code

### Priority
🟡 Medium

### Description
Platform-specific code (`#ifdef _WIN32`, `#ifdef __linux`, etc.) is scattered throughout the codebase:
- `builtins.c` - PATH_MAX, getcwd differences
- `lexer.h` - strcasecmp vs _stricmp
- `vm.h` - UNUSED attribute handling
- Multiple files - path separator handling

This makes the code harder to maintain and port to new platforms.

### Proposed Implementation
1. Create `platform.h/c` with unified interfaces:
   ```c
   // platform.h
   #ifndef __PLATFORM_H
   #define __PLATFORM_H
   
   // Path constants
   #define SK_PATH_MAX      4096
   #define SK_PATH_SEP      '/'
   #define SK_PATH_SEP_STR  "/"
   
   // String functions
   int sk_strcasecmp(const char* s1, const char* s2);
   char* sk_strdup(const char* s);
   
   // File path functions
   char* sk_getcwd(char* buf, size_t size);
   char* sk_normalize_path(const char* path);
   
   // Compiler attributes
   #ifdef _WIN32
   #   define SK_UNUSED 
   #else
   #   define SK_UNUSED __attribute__((unused))
   #endif
   
   #endif
   ```

2. Implement platform-specific code once in `platform.c`:
   ```c
   #ifdef _WIN32
   int sk_strcasecmp(const char* s1, const char* s2) {
       return _stricmp(s1, s2);
   }
   #else
   int sk_strcasecmp(const char* s1, const char* s2) {
       return strcasecmp(s1, s2);
   }
   #endif
   ```

3. Replace all platform-specific code:
   - `#ifdef _WIN32` blocks → call `sk_*` functions
   - Remove duplicate platform checks
   - Use `SK_PATH_MAX` instead of `PATH_MAX`/`_MAX_PATH`

4. Add platform detection macros:
   ```c
   #define SK_PLATFORM_WINDOWS
   #define SK_PLATFORM_LINUX
   #define SK_PLATFORM_MACOS
   ```

### Testing & Acceptance Criteria
- [ ] All platform-specific `#ifdef` consolidated to `platform.c`
- [ ] Builds without warnings on Windows (MSVC)
- [ ] Builds without warnings on Linux (GCC)
- [ ] Builds without warnings on macOS (Clang)
- [ ] Path handling works on all platforms
- [ ] File operations work on all platforms
- [ ] No `_WIN32`, `__linux`, `__APPLE__` in non-platform files
- [ ] Tests pass on all three platforms
- [ ] Code review: platform abstraction is clear and maintainable

---

## Issue #9: Document VM Architecture and Opcodes

### Priority
🟡 Medium

### Description
The VM implementation lacks documentation for:
- Bytecode format and encoding
- Opcode semantics and stack effects
- GC algorithm details (mark-sweep phases)
- Object representation and tagging scheme
- Memory layout of heap objects

This makes it difficult to:
- Understand code generation from parser
- Debug bytecode execution issues
- Extend with new opcodes
- Verify GC correctness

### Proposed Implementation
1. Add comprehensive `vm.h` header comments:
   ```c
   /**
    * VM Architecture
    * ===============
    * Stack-based bytecode VM with mark-sweep garbage collection.
    * 
    * Object Representation:
    * - Small integers: tagged pointers (bottom 2 bits = 01)
    * - Booleans: tagged pointers (bottom 2 bits = 10)
    * - Heap objects: aligned pointers (bottom 2 bits = 00)
    * 
    * Stack Layout:
    * - Fixed size VM_STACK_SIZE (1024 elements)
    * - Grows upward from stack[0]
    * - sp points to next free slot
    */
   ```

2. Document each opcode with stack effects:
   ```c
   enum {
       OP_HALT,        // [] -> [] - Stop execution
       OP_PUSH,        // [] -> [obj] - Push next code word as object
       OP_POP,         // [obj] -> [] - Discard top of stack
       OP_NUM,         // [] -> [num] - Push next word as tagged number
       OP_CALL,        // [proc arg1 ... argN] -> [result] - Call function
       // ... etc
   };
   ```

3. Document GC algorithm in `vm.c`:
   ```c
   /**
    * Garbage Collection: Mark and Sweep
    * ===================================
    * 
    * Mark Phase:
    * 1. Mark all objects reachable from roots:
    *    - VM stack entries
    *    - Global variable table
    *    - Current code chunk
    * 2. Use gray stack for iterative marking (avoid recursion)
    * 
    * Sweep Phase:
    * 1. Walk all allocated objects
    * 2. Free unmarked objects
    * 3. Unmark marked objects for next cycle
    * 
    * Triggers:
    * - bytesAllocated exceeds nextGC threshold
    * - VM_STRESS_GC: collect on every allocation (debug only)
    */
   ```

4. Create `docs/vm-architecture.md` with diagrams:
   - Object memory layout
   - Stack frame structure
   - Bytecode encoding examples
   - GC object graph example

5. Add inline comments for complex code:
   - Pointer tagging/untagging operations
   - Environment chain traversal
   - Symbol table hash function

### Testing & Acceptance Criteria
- [ ] Every opcode documented with stack effect notation
- [ ] Object tagging scheme fully explained
- [ ] GC algorithm described with examples
- [ ] `docs/vm-architecture.md` created with diagrams
- [ ] Code comments added to 10+ complex functions
- [ ] New developer can understand bytecode from docs
- [ ] VM debugging guide added to documentation
- [ ] Examples of reading disassembled bytecode

---

## Issue #10: Add Continuous Integration with Multiple Platforms

### Priority
🟡 Medium

### Description
While GitHub Actions badge exists in README, it's unclear if CI is fully configured. Need comprehensive CI that:
- Tests on Windows, Linux, macOS
- Runs both debug and release builds
- Checks for memory leaks and undefined behavior
- Enforces code quality standards
- Reports test coverage

### Proposed Implementation
1. Create `.github/workflows/ci.yml`:
   ```yaml
   name: CI
   
   on: [push, pull_request]
   
   jobs:
     build:
       strategy:
         matrix:
           os: [ubuntu-latest, windows-latest, macos-latest]
           build_type: [Debug, Release]
       
       runs-on: ${{ matrix.os }}
       
       steps:
         - uses: actions/checkout@v4
           with:
             submodules: recursive
         
         - name: Configure CMake
           run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}
         
         - name: Build
           run: cmake --build build
         
         - name: Test
           run: ctest --test-dir build --output-on-failure
   ```

2. Add sanitizer job (Linux only):
   ```yaml
     sanitize:
       runs-on: ubuntu-latest
       steps:
         - uses: actions/checkout@v4
           with:
             submodules: recursive
         
         - name: Configure with sanitizers
           run: |
             cmake -B build \
               -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
         
         - name: Build
           run: cmake --build build
         
         - name: Test
           run: ctest --test-dir build --output-on-failure
   ```

3. Add code coverage job:
   ```yaml
     coverage:
       runs-on: ubuntu-latest
       steps:
         - uses: actions/checkout@v4
           with:
             submodules: recursive
         
         - name: Install lcov
           run: sudo apt-get install -y lcov
         
         - name: Configure
           run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
         
         - name: Build and test
           run: |
             cmake --build build
             ctest --test-dir build
         
         - name: Generate coverage
           run: |
             lcov --capture --directory build --output-file coverage.info
             lcov --remove coverage.info '/usr/*' --output-file coverage.info
         
         - name: Upload to Codecov
           uses: codecov/codecov-action@v3
   ```

4. Add status badges to README.md:
   ```markdown
   ![CI Status](https://github.com/USER/skeme/workflows/CI/badge.svg)
   ![Coverage](https://codecov.io/gh/USER/skeme/branch/main/graph/badge.svg)
   ```

### Testing & Acceptance Criteria
- [ ] CI runs on push to main branch
- [ ] CI runs on all pull requests
- [ ] Tests pass on Ubuntu, Windows, macOS
- [ ] Both Debug and Release builds tested
- [ ] AddressSanitizer detects no memory errors
- [ ] UndefinedBehaviorSanitizer detects no UB
- [ ] Code coverage report generated and uploaded
- [ ] Build status badge shows in README
- [ ] Failed builds block PR merging
- [ ] CI completes in <5 minutes

---

## Issue #11: Implement Tail Call Optimization

### Priority
🟢 Low (but important for Scheme semantics)

### Description
Tail call optimization (TCO) is essential for Scheme - recursive functions in tail position should not grow the call stack. Without TCO:
- Recursive algorithms hit stack overflow
- Iterative patterns via recursion are impractical
- Not compliant with Scheme standards (R5RS requires TCO)

Currently listed in `todo.md` but not implemented.

### Proposed Implementation
1. Detect tail calls during parsing:
   ```c
   // In parser - when expr is in tail position:
   bool in_tail_position = (parent_context == TAIL_CONTEXT);
   if (in_tail_position && expr_type == CALL_EXPR) {
       vm_emit(pvm, OP_TAIL_CALL);
   }
   ```

2. Add tail call opcodes:
   ```c
   enum {
       // ... existing opcodes
       OP_TAIL_CALL,    // Reuse current stack frame
       OP_TAIL_RET      // Return without pushing frame
   };
   ```

3. Modify VM to reuse stack frames:
   ```c
   case OP_TAIL_CALL:
       // Instead of pushing new frame:
       // 1. Pop current frame
       // 2. Set up args for next call
       // 3. Jump to function without growing call stack
       break;
   ```

4. Identify tail contexts in parser:
   - Last expression in function body
   - Last expression in `if` branches
   - Last expression in `cond` clauses
   - NOT tail: arguments to functions

5. Add `-ftco` flag to enable/disable for debugging

### Testing & Acceptance Criteria
- [ ] Tail recursive factorial(100000) doesn't overflow stack
- [ ] Mutual tail recursion works (even/odd functions)
- [ ] Non-tail calls still work correctly
- [ ] Tail call in `if` branches optimized
- [ ] Tail call in `cond` clauses optimized
- [ ] `(define (loop) (loop))` runs infinitely without overflow
- [ ] Stack depth remains constant in tail recursive loop
- [ ] Test: fibonacci as tail-recursive (fib-iter) vs non-tail
- [ ] Benchmark: tail-call version same speed as iterative
- [ ] At least 30 tail-call test cases in `test.scm`

---

## Issue #12: Dynamic Symbol Table Growth

### Priority
🟢 Low

### Description
Symbol table uses fixed-size array `Symbol_t symbols[MAX_SYMBOLS]` (4096 entries). This:
- Wastes memory for small programs
- Crashes with `assert()` on overflow for large programs
- Cannot be configured at runtime

Related to Issue #1 (error handling) and Issue #2 (state management).

### Proposed Implementation
1. Make symbol table dynamic in `SKState`:
   ```c
   struct SKState {
       Symbol_t* symbols;
       int symbol_count;
       int symbol_capacity;
       // ...
   };
   ```

2. Start with reasonable default (256 symbols):
   ```c
   int sk_init(SKState* sk, int argc, char* argv[]) {
       sk->symbol_capacity = 256;
       sk->symbol_count = 0;
       sk->symbols = malloc(sizeof(Symbol_t) * sk->symbol_capacity);
       // ...
   }
   ```

3. Grow on demand using existing `GROW_ARRAY` pattern:
   ```c
   int add_symbol(SKState* sk, const char* name, int lineno) {
       if (sk->symbol_count >= sk->symbol_capacity) {
           int old_cap = sk->symbol_capacity;
           sk->symbol_capacity = GROW_CAPACITY(old_cap);
           sk->symbols = GROW_ARRAY(Symbol_t, sk->symbols, 
                                     old_cap, sk->symbol_capacity);
       }
       // ... add symbol
   }
   ```

4. Consider hash table instead of linear array:
   - Use existing `hash/` library
   - O(1) lookup instead of O(n) linear search
   - Better for large programs

5. Free in shutdown:
   ```c
   int sk_shutdown(SKState* sk) {
       free(sk->symbols);
       sk->symbols = NULL;
       // ...
   }
   ```

### Testing & Acceptance Criteria
- [ ] Symbol table starts small (256 entries)
- [ ] Automatically grows when full
- [ ] Can handle 10,000+ symbols without error
- [ ] No memory leaks in symbol table growth
- [ ] Symbol lookup still works after growth
- [ ] Test: add symbols in loop until >4096 entries
- [ ] Test: memory usage reasonable for small programs
- [ ] Benchmark: symbol lookup performance acceptable
- [ ] Consider: replace with hashtable for O(1) lookup

---

## Summary Table

| Priority | Issue | Impact | Effort |
|----------|-------|--------|--------|
| 🔴 Critical | #1 Error Handling | Prevents crashes | Medium |
| 🔴 Critical | #2 State Management | Enables testing, multiple instances | High |
| 🟡 High | #3 GC Stress | 10-100x performance improvement | Low |
| 🟡 High | #4 Lambda | Core language feature | High |
| 🟡 High | #5 Unit Tests | Enables confident refactoring | Medium |
| 🟡 High | #6 Build System | Reduces maintenance | Low |
| 🟡 Medium | #7 Error Infrastructure | Consistency, flexibility | Medium |
| 🟡 Medium | #8 Platform Abstraction | Maintainability | Medium |
| 🟡 Medium | #9 Documentation | Developer productivity | Low |
| 🟡 Medium | #10 CI/CD | Quality assurance | Medium |
| 🟢 Low | #11 Tail Calls | Scheme compliance | High |
| 🟢 Low | #12 Dynamic Symbols | Scalability | Low |

## Recommended Implementation Order
1. **#3** - GC Stress (quick win, huge performance impact)
2. **#1** - Error Handling (safety foundation)
3. **#2** - State Management (architectural improvement)
4. **#6** - Build System (reduce confusion)
5. **#5** - Unit Tests (quality infrastructure)
6. **#10** - CI/CD (automated quality checks)
7. **#4** - Lambda (core feature)
8. **#11** - Tail Calls (Scheme compliance)
9. **#7, #8, #9, #12** - Quality improvements
