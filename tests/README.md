# Skeme C Unit Tests

This directory contains unit tests for the Skeme interpreter's C codebase.

## Test Files

- **test_main.c** - Main test runner that coordinates all test modules
- **test_vm.c** - VM operations, stack management, opcode emission, object creation
- **test_gc.c** - Garbage collection: marking, sweeping, stack/global references
- **test_lexer.c** - Symbol table operations, capacity management, file stack
- **test_parser.c** - Parser state initialization and error tracking
- **test_builtins.c** - Builtin function registration and lookup
- **test_helpers.c** - Global variables needed for testing

## Running Tests

### Build and run all tests:
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

### Run unit tests only:
```bash
./build/skeme_tests
```

### Build with coverage:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/skeme_tests
```

## Test Framework

Tests use the **testy** framework (located in `hash/testy/`), which provides:
- `MODULE()` - Define a test module
- `SUITE()` - Define a test suite
- `TEST()` - Execute a test assertion
- `COMMENT()` - Add descriptive comments

## Test Statistics

- **Total Tests**: 435 unit tests
- **Test Modules**: 5 (VM, GC, Lexer, Parser, Builtins)
- **Test Suites**: 27 focused test suites
- **Execution Time**: ~0.01 seconds
- **Coverage**: Tests cover core VM operations, garbage collection, parsing, and builtins

## Coverage

Code coverage is enabled for Debug builds. After running tests with coverage:

```bash
cd build/CMakeFiles/skeme_tests.dir
gcov vm.c.gcda parser.c.gcda builtins.c.gcda
```

## Test Philosophy

- Tests focus on unit-level validation of core components
- Integration tests exist separately in `test.scm`
- Tests are designed to be fast (<1 second total execution)
- Each test is independent and can run in isolation
