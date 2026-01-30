# Skeme AI Coding Guidelines

## Architecture Overview
Skeme is a Scheme-like language interpreter in C with a classic compiler architecture:
- **Lexer** (`lexer.c/h`): Tokenizes input, manages symbol table and file includes
- **Parser** (`parser.c/h`): Builds AST from tokens using yacc/bison grammar
- **VM** (`vm.c/h`): Bytecode execution with mark-sweep GC, stack-based evaluation
- **Builtins** (`builtins.c`): Core functions like arithmetic, lists, I/O
- **I/O** (`io.c`): File and console operations

State is managed through `SKState` struct. VM uses open-addressing hashtable (`hash/`) for globals and symbols.

## Build & Test Workflow
- **Build**: Use CMake (`cmake -S . -B build && cmake --build build`) or Makefile (`make`)
- **Test**: Run `skeme -v test.scm` (uses `skunit.scm` framework with `test-case`, `test-suite`, `test-end`)
- **Interactive**: `skeme -i` for REPL
- **Debug**: `-d` for parser debug, `-p` for parse logging, VM logging via `(vmlog)` in Scheme

## Code Patterns
- **Scheme Extensions**: Load with `(load "file.scm")`, e.g., `(load "stdlib.scm")` for standard library
- **Testing**: Use `test-case "description" (assertion)`, e.g., `test-case "even?" (and (even? 2) (not (even? 3)))`
- **Error Handling**: `assert` calls `error` on failure, e.g., `(assert condition "message")`
- **Memory**: VM handles GC; use `vm_realloc` for dynamic arrays, `GROW_ARRAY` macro for expansion
- **Symbols**: Stored in global `symbols[]` array, hashed for lookup

## Key Files
- `main.c`: CLI parsing and REPL loop
- `vm.c`: Core execution, GC (`vm_gc`), stack ops
- `stdlib.scm`: Standard functions like `apply`, `filter`, `cadr`
- `test.scm`: Comprehensive test suite examples
- `hash/`: Submodule hashtable library (open-addressing, opaque keys/values)

## Conventions
- C standard: C99, Windows compatibility (`_CRT_SECURE_NO_WARNINGS`)
- Scheme: R5RS-like, with extensions for I/O and testing
- Naming: `SK_` prefixes for types, `g_` for globals, `vm_` for VM functions
- Includes: Local headers first, then system

Focus on VM bytecode generation and GC integration when adding features. Test new builtins in `test.scm`.</content>
<parameter name="filePath">c:\dev\skeme\.github\copilot-instructions.md