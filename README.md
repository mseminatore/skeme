# Skeme

[![CMake](https://github.com/mseminatore/skeme/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/mseminatore/skeme/actions/workflows/cmake-single-platform.yml)
![GitHub License](https://img.shields.io/github/license/mseminatore/skeme)

Skeme is a lightweight Scheme-like language interpreter written in C. It features
a single-pass compiler that emits bytecode for a stack-based virtual machine with
tail-call optimization, mark-sweep garbage collection, closures, and a REPL.

## Features

- **R5RS-inspired** — lists, lambdas, closures, tail-call optimization, `cond`/`case`
- **Bytecode VM** — compiled to compact bytecode with optimized dispatch
  (computed gotos on GCC/Clang, function pointer table on MSVC)
- **Garbage collection** — tri-color mark-sweep GC with automatic thresholds
- **First-class functions** — lambdas, closures with captured upvalues, higher-order functions
- **Numeric types** — integers and IEEE 754 floats with tagged pointer representation
- **String interning** — strings are interned for fast equality checks
- **REPL** — interactive read-eval-print loop with colored output
- **Extensible** — register custom C types and functions
  (see [Extending Skeme](EXTENDING.md))
- **Cross-platform** — builds on Windows (MSVC), Linux (GCC/Clang), and macOS

## Getting Started

```sh
git clone https://github.com/mseminatore/skeme.git
cd skeme
git submodule update --init --recursive
```

## Building

### CMake (recommended)

```sh
cmake -S . -B build
cmake --build build --config Release
```

The binaries are placed in `build/Release/` (MSVC) or `build/` (Make-based
generators).

### Make (Linux / macOS)

```sh
make
```

## Usage

```
skeme [options] [inputfile]
```

### Options

| Flag       | Description                          |
|------------|--------------------------------------|
| `-i`       | Start interactive REPL               |
| `-v`       | Enable verbose output                |
| `-d`       | Enable parser debug output           |
| `-p`       | Enable parser logging                |
| `-profile` | Print VM profiling report on exit    |
| `-h`       | Display usage help                   |

### Running a Script

```sh
skeme myprogram.scm
```

### Interactive REPL

```sh
skeme -i
```

Or simply run `skeme` with no arguments to enter the REPL. Use `(exit)` to quit.

### Loading Libraries

Use `load` at the top of your script to pull in additional definitions:

```scheme
(load "stdlib.scm")
```

## Standard Library

The file `stdlib.scm` provides common utility functions on top of the builtins:

| Function    | Description                                 |
|-------------|---------------------------------------------|
| `apply`     | Apply a function to each element of a list  |
| `filter`    | Return elements satisfying a predicate      |
| `append`    | Concatenate two lists                       |
| `cadr`      | `(car (cdr x))`                             |
| `cddr`      | `(cdr (cdr x))`                             |
| `even?`     | Test if a number is even                    |
| `odd?`      | Test if a number is odd                     |
| `min`       | Return the smaller of two numbers           |
| `max`       | Return the larger of two numbers            |
| `assert`    | Assert a condition or raise an error        |
| `list-ref`  | Return the nth element of a list            |
| `list-tail` | Return sublist after dropping n elements    |
| `member`    | Find first element equal? to a value        |
| `memq`      | Find first element eq? to a value           |
| `assoc`     | Look up key in association list (equal?)    |
| `assq`      | Look up key in association list (eq?)       |
| `for-each`  | Apply function to each element (side effects)|
| `gcd`       | Greatest common divisor                     |
| `lcm`       | Least common multiple                       |

### Built-in Functions

The following functions are implemented natively in C:

**Arithmetic:** `+`, `-`, `*`, `/`, `=`, `<`, `>`, `<=`, `>=`, `modulo`,
`abs`, `quotient`, `remainder`, `expt`, `sqrt`, `floor`, `ceiling`,
`truncate`, `round`

**List operations:** `cons`, `car`, `cdr`, `list`, `length`, `reverse`,
`set-car!`, `set-cdr!`, `map`

**Type predicates:** `null?`, `pair?`, `list?`, `number?`, `integer?`,
`float?`, `boolean?`, `char?`, `string?`, `atom?`, `zero?`, `positive?`,
`negative?`, `procedure?`

**Equality:** `eq?`, `eqv?`, `equal?`

**Boolean:** `not`

**Strings:** `string-append`, `string-length`, `string-ref`, `substring`,
`string-copy`, `string->number`, `number->string`, `string->float`,
`float->string`

**Characters:** `char->integer`, `integer->char`

**I/O:** `display`, `write`, `newline`, `read`, `read-line`, `read-char`,
`write-char`, `write-line`, `printf`, `open-input-file`, `open-output-file`,
`close-file`, `delete-file`

**Control:** `eval`, `error`, `exit`

**Introspection:** `typeof`, `disasm`, `gc`, `gc-stats`, `mem-allocated`,
`getenv`, `command-line`, `pwd`

## Extending Skeme

Skeme supports registering custom C types and native functions. You can add new
data types (with GC integration, printing, and equality) and expose C functions
to Scheme code.

See [**EXTENDING.md**](EXTENDING.md) for the full API reference and examples.

## Testing

### Scheme Test Suite

```sh
skeme -v test.scm
```

Runs the built-in test suite using the `skunit.scm` test framework.

### C Unit Tests

```sh
# CMake
ctest --test-dir build -C Release

# Make
make unit-test
```

### Run All Tests

```sh
# Make
make all-tests
```

## License

[MIT](LICENSE) — Copyright (c) 2025 Mark Seminatore
