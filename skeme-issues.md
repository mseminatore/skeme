# Skeme issues to file

## 1. ✅ Closures lose captured variables after calling another function

**Labels:** bug
**Status:** Complete. Fixed in f823510; regression tests are under "closure:" in `test.scm`. See also #10: closures now share captured variables instead of copying them.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

A closure that calls another user function and *then* reads a captured variable fails with `OP_GET_UPVALUE: invalid upvalue index`. `let` compiles to a closure over the enclosing locals, so it hits the same bug.

```scheme
(define (id x) x)
(define (mk a) (lambda () (+ (id 5) a)))
(display ((mk 10)))
```
Expected `15`. Actual: `ERROR: 'OP_GET_UPVALUE: invalid upvalue index'` and `5` is printed.

Reading the captured variable *before* the call works, which points at the cause:

```scheme
(define (mk a) (lambda () (+ a (id 5))))   ; => 15
```

Same failure through `let`:

```scheme
(define (mk a) (lambda () (let ((b (id 1))) (+ b a))))
(display ((mk 10)))        ; ERROR: invalid upvalue index
```

A variant crashes outright:

```scheme
(define (mk a) (let ((inc (lambda (x) (+ x a)))) (lambda () (list (inc 1) (inc 2) a))))
(display ((mk 10)))
; skeme: vm.h: vm_fp_peek: Assertion `pvm->fp - 1 - distance >= 0 && "Stack underflow on fp_peek"' failed.
```

### Cause
There were two separate bugs.

**A. `current_closure` was never restored on return.** `vm_call_procedure` and `op_tailcall*` set `pvm->current_closure`, but `op_ret` left it alone, so after a nested call the caller's upvalue accesses saw the callee's closure (or `NULL`). This caused the first three repros. Relatedly, the GC never marked `current_closure`. A closure called straight from an expression such as `((mk 10))` is popped by `ICALL`, so while it ran it could be collected.

**B. Calling a captured variable read the wrong frame.** The call-head code in `expr()` found a function-valued variable with `vm_get_local_var`, which searches every enclosing scope, and then emitted `OP_GET_LOCAL` with the *outer* function's slot index. The inner closure then read that index from its own frame. This caused the crashing variant. It fails without `let` too:

```scheme
(define (mk f) (lambda () (f 5)))
(display ((mk (lambda (x) x))))   ; fp_peek underflow, or "TAILCALL_I: not a procedure"
```

### Fix
* **A (`vm.c`):** the frame's `fp-1` slot used to hold the arity as a number. It now holds the callee's `Procedure`/`Closure`, giving the frame `[args..., ret_addr, saved_fp, fn]`, and the arity is read from that object (`frame_arity`). `op_ret` restores `current_closure` from the caller's own frame (`frame_closure`), or sets `NULL` at top level. The same-arity tail-call path writes the new callee into the reused frame, and `vm_call_function` pushes the function object as well. Because the running closure is now on the stack, the GC marks it. The frame size is unchanged, so the parser's `+3` local offsets still hold.
* **B (`parser.c`):** the call head now uses `vm_get_local_var_ex`. When the function comes from an enclosing scope, it calls `register_upvalue` and emits `OP_GET_UPVALUE`, the same way plain variable references already worked.

### Verification
All four repros above now give the expected results: `15`, `15`, `11`, `(11 12 10)`. `test.scm` passes 323/323, including 9 new `closure:` regression tests, and the C unit tests pass (185 tests, 0 failures). `examples/forth.scm` and `examples/metacircular.scm` run without errors, and 100k-deep tail recursion through `if`, `cond`, `case`, `let`, `let*`, `begin`, `and` and `or` is unaffected.

### Impact (before the fix)
Any non-trivial closure code broke, e.g. a `let` whose body calls a function and then uses an outer variable. The examples avoid `let` in the evaluator for this reason; that workaround is no longer needed.

---

## 2. ✅ A syntax error inside a form aborts the process (stack-underflow assertion)

**Labels:** bug
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

After a parse error, the code emitted for the partial form is still executed, and the VM aborts instead of recovering.

```scheme
(display (quote (1 . 2)))
```
```
ERROR: 'unexpected token in quoted expression' in file ... near line 1
skeme: vm.h:423: vm_pop: Assertion `pvm->sp > 0 && "Stack underflow"' failed.
```

### Expected
Report the error and discard the form (or the whole chunk) without running it, then continue with the next top-level form (or stop with a non-zero exit in script mode).

### Notes
Related to #3 / #17, which replaced assertions for chunk and lifecycle functions; the stack `vm_push`/`vm_pop` assertions are still fatal.

### Cause
The parser had no error recovery. After an error it kept parsing, and the bytecode compiled for the broken form stayed in the chunk and ran. The stray or missing values then underflowed the stack. Separately, the parser's `while (LOOKAHEAD != ')')` loops never stopped at the end of the input, so a form left unclosed after an error made the process spin forever.

### Fix
* `yyparse` records the chunk offset and error count before each top-level form. If the form produced errors, `discard_form` truncates its bytecode, restores the parser's top-level state, and skips the rest of the form. The lexer now counts parens (`paren_depth`, maintained in `yylex`) so the skip knows where the form ends. The following forms still compile and run.
* The body loops also stop at `DONE`.
* A script run that reported errors exits with status 1. The REPL is unaffected.
* Stack underflow is no longer an assertion either (see #3), so code that does go wrong cannot take the process down.

### Verification
`(display (quote (1 . 2))) (display 42)` reports the error and prints `42`, exit status 1. A broken form in the middle of a file, an undefined name, a stray `)` and an unclosed form at the end of the file each drop only the bad form. C tests: "Parse Error Recovery" in `tests/test_error.c`.

---

## 3. ✅ Stack overflow aborts the process; default 1024-slot stack allows only ~100 non-tail recursion levels

**Labels:** bug, enhancement
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

```scheme
(define (count n) (if (= n 0) 0 (+ 1 (count (- n 1)))))
(display (count 100))    ; 100
(display (count 300))    ; abort
```
```
skeme: vm.h:417: vm_push: Assertion `pvm->sp < VM_STACK_SIZE && "Stack overflow"' failed.
```

Two problems:

1. **Overflow is fatal** (an `assert`, which disappears entirely under `-DNDEBUG`). It should be a catchable runtime error, like the other VM errors. Follows on from #3 / #17.
2. **`VM_STACK_SIZE` is 1024 slots.** Each call uses the argument slots plus 3 frame slots, so ordinary list recursion over a few hundred elements fails. A recursive-descent evaluator nests about 5 frames per interpreted call, so only ~20 interpreted calls fit.

### Suggestions
* Check for overflow in `vm_push` / call setup and return `VM_ERROR` with a "stack overflow" message, unwinding to the top level.
* Raise the default (e.g. 16K-64K slots) and/or grow the stack dynamically.
* `VM_STACK_SIZE` is now overridable with `-DVM_STACK_SIZE=...` (`#ifndef` guard in `vm.h`).

### Fix
* **Overflow and underflow are runtime errors in every build.** `vm_push`, `vm_pop`, `vm_peek`, `vm_fp_peek` and `vm_fp_set` check their bounds and, instead of asserting, call `vm_abort`. That reports the error once and sets `abort_exec`. `vm_exec` and the C re-entry loop in `vm_call_function` (used by natives such as `map`) stop when it is set, and the outermost one unwinds: it clears the stack and frames and closes upvalues. So an overflow inside a `map` callback unwinds to the top level as well. The REPL then carries on; a script stops at that point and exits with status 1. Other runtime errors (wrong argument types and so on) still print and continue as before.
* **The stack is bigger and on the heap.** `VM_STACK_SIZE` now defaults to 64K slots (about 10-15K nested non-tail calls), still overridable. `vm_init` allocates it and `vm_shutdown` frees it, so `SKState` does not grow (the unit tests keep it on the C stack).
* `vm_abort` is also used for unbound variables (#6).

### Verification
`(count 10000)` returns `10000`. `(count 1000000)` reports `stack overflow (recursion too deep)` instead of aborting, and the REPL keeps working. C tests: "Stack Overflow" in `tests/test_error.c` covers recovery afterwards and an overflow inside `map`.

### Performance cost
On `main` the checks were `assert`s, so Release builds had none. Checking every push is not free.

| Benchmark (Release, min of 9 runs) | `main` | now |
|---|---|---|
| `bench.scm` | 93 ms | 98 ms (about 5% slower) |
| `fib(32)` | 491-505 ms | 621-627 ms (about 25% slower) |

On `fib(32)`, about 15 of those points are the overflow check in `vm_push`, measured by removing just that check. The remaining ~8% comes from the other fixes together: the stack behind a pointer, the abort checks on calls and returns, closing upvalues on return, and the extra dispatch for global calls. Two things keep it down: the overflow path is out of line, and an abort sends `ip` to an `OP_HALT` instead of the dispatch loop testing a flag before every instruction (that test alone cost about 5%).

To get the push check off the hot path, do what Lua does: have the compiler compute each function's maximum stack depth, and check for that much room once per call instead of on every push.

**Update (#17):** tried and reverted. On the current VM it gained only about 2% on `fib(34)`, within noise, so the per-push check stays; see #17.

---

## 4. ✅ Tail calls are only optimized when caller and callee take the same number of arguments

**Labels:** bug, enhancement
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

`op_tailcall` / `op_tailcall_i` reuse the frame only when `nargs == old_arity`; otherwise they fall back to a normal call (`TAILCALL-FALLBACK`), so the stack still grows. Self-recursion is fine, but so is *any* tail call between functions of equal arity:

```scheme
(define g #f)
(define (f n) (if (= n 0) 0 (g (- n 1))))
(set! g (lambda (n) (if (= n 0) 0 (f (- n 1)))))
(display (f 100000))     ; 0, constant stack
```

With different arities the same pattern overflows:

```scheme
(define g #f)
(define (f n acc) (if (= n 0) acc (g (- n 1))))
(set! g (lambda (n) (if (= n 0) 0 (f (- n 1) 0))))
(display (f 100 0))      ; 0
(display (f 2000 0))     ; Stack overflow assertion
```

R5RS requires proper tail calls in general. The frame layout (`[args][ret ip][old fp][arity]`) makes a differing arity awkward because the argument block changes size, but it looks doable by moving the new arguments down to `fp - 3 - old_arity`, then rewriting the three header slots and adjusting `fp`/`sp`.

### Workaround
Give every function in a tail-call chain the same number of parameters. `examples/forth.scm` does this (every function in its main loop takes one token-list argument). No longer needed.

### Fix
`op_tailcall` and `op_tailcall_i` now share `vm_tail_call`, which handles any arity the way suggested above. It reads the caller's return address and saved `fp` first (the move can overwrite them when there are more arguments), closes the old frame's upvalues, moves the new arguments down to the start of the old frame, rewrites the header and sets `fp`/`sp`. The new frame never extends past the current `sp`, so this needs no extra stack.

A second gap turned up while testing: a call whose head is an expression, such as `((make-f) x)`, compiles to `OP_XCALL`, which was never turned into a tail call at all. The new `OP_TAILCALL_X` is its tail form.

### Verification
The repro above runs `(f 1000000 0)` in constant stack. Tail calls to functions with more or fewer arguments, through closures and through a computed head (`((mk) (- n 1))`) run 100K-200K deep. Tests: "tail calls between different arities", "tail call to a function with more arguments" and "tail call through a computed function" in `test.scm`.

---

## 5. ✅ `display` and `newline` push no result, which unbalances the stack in `begin` and nested calls

**Labels:** bug
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

Most native functions leave one value on the stack, but `display` and `newline` leave none (`sk_display` pops its argument and returns without pushing). Code that assumes every expression yields a value then pops something that belongs to the caller.

```scheme
(display (display 1))
; 1
; skeme: vm.h: vm_pop: Assertion `pvm->sp > 0 && "Stack underflow"' failed.

(display (newline))      ; same abort
```

`begin_expr` emits `OP_POP` between expressions, so any `begin` with `display` in a non-final position corrupts the stack. In a loop that overflows the stack:

```scheme
(define (loop i)
  (begin (display "") (if (< i 100000) (loop (+ i 1)) i)))
(display (loop 0))
; Stack overflow assertion
```

(`printf` pushes `#t`, so the natives are not consistent with each other.)

### Suggestion
Give every native a defined result. Either push a value from `display`/`newline`/etc., or have the VM know each native's result count and push a placeholder for void ones. Note the REPL prints the top of the stack after every top-level expression, so a void native should push something the printer skips (e.g. an "unspecified" object) to avoid new `false` noise.

`op_ret` now reads the frame header relative to `fp` instead of the top of the stack, which makes returns robust to leftover values, but `begin` still pops blindly.

### Fix
The second option: the VM guarantees one result per native call, for every native including user extensions. After a native returns, `vm_native_result` compares the stack depth with what one result would give and, if the native pushed nothing, pushes the new **unspecified** value. That value is an immediate (`TAG_UNSPEC`, one of the reserved tags), so it costs no allocation. `display` prints it as `#<unspecified>`, it counts as true, and the REPL does not echo it. A native called with the wrong number of arguments also leaves an unspecified result in place of its arguments.

The loop above crashed with a segfault on `main`, not just the assertion.

### Verification
`(display (display 1))` prints `1#<unspecified>`; the `begin` loop returns `100000`. A temporary check over the test suite, both examples, `bench.scm` and `stdlib.scm` found no native that leaves anything other than zero or one result. Tests: "display yields one value" and "display in a begin inside a loop" in `test.scm`; "Unspecified Results" in `tests/test_error.c`.

---

## 6. ✅ `define` of an existing global does not update already-compiled references; forward references are compile errors

**Labels:** bug, enhancement
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

Two related behaviours around global lookup:

**1. Redefining a global leaves older code pointing at the old value.**

```scheme
(define b #f)
(define (a x) (b x))
(define (b x) (+ x 1))
(display (a 1))
; ERROR: 'TAILCALL_I: not a procedure'
```
In Scheme a top-level `define` of an existing variable assigns it, so this should print `2`. Here the second `define` creates a new slot and `a` keeps calling `#f`. `set!` works: `(define b #f)` ... `(set! b (lambda (x) (+ x 1)))`.

**2. Referring to a global that is not defined yet is an error**, so mutually recursive top-level functions are impossible without a stub:

```scheme
(define (a x) (b x))
(define (b x) (+ x 1))
; ERROR: 'entity undefined' ... then 'unexpected character' / 'syntax error' cascade
```

### Suggestion
Resolve global references by name at run time (create the binding on first reference, error on use if still unbound), and make `define` of an existing global assign it. This also removes the cascade of follow-on errors after the first "entity undefined".

### Workaround
Forward-declare with `(define f #f)` and fill in with `(set! f (lambda ...))`. The examples do this. No longer needed.

### Cause
Not a new slot: global names are interned, so both defines use the same key. Three things combined:
* A top-level **function** `define` bound its name only at compile time, while a **variable** `define` binds when it runs. A script is compiled in full before any of it runs, so `(define b #f)` ran last and overwrote the procedure.
* A call to a global that was a procedure at compile time embedded that procedure in the bytecode (`OP_CALL proc`), so later redefinitions never reached it.
* An unknown name was a compile-time error, and that call head stopped without parsing its arguments, which caused the cascade.

Relatedly, a variable `define` set the global to nil at compile time, so `(define x 5) (define x (+ x 1))` failed as well.

### Fix
* **Globals are looked up by name at run time.** References to unknown names compile to a run-time lookup. The compiler adds an **unbound** placeholder entry (`TAG_UNBOUND`, the last reserved tag), which also keeps the interned name alive for the GC. Reading a global that is still unbound when the code runs is an `unbound variable 'x'` runtime error (through `vm_abort`, see #3). `set!` of a global defined later works too.
* **Calls to user procedures look the name up when they run.** Natives and the inlined arithmetic ops are still bound at compile time.
* **Top-level function defines also store when they run**, so defines take effect in program order. Each compiled procedure is also rooted under a unique internal name, because the emitted code refers to it directly while a later define may rebind the real name first.
* **A variable `define` only adds the placeholder when the name is new**; it no longer overwrites an existing value at compile time.
* **The lookup has an inline cache.** The fused `OP_LOAD_GLOBAL key cached_value cache_version` caches the value it last read; every global write bumps `globals_version`, which invalidates all caches. Without the cache, `fib(30)` was about 18% slower. With it, it runs at the old speed.

### Limitation
Natives are still bound when a call is compiled, so redefining a builtin such as `display` does not affect code compiled before the redefinition.

### Verification
Both repros print `2`. `(define x 5) (define x (+ x 1))` gives `6`, and mutually recursive top-level functions run 100K deep. Tests: "forward reference to a later define", "redefinition reaches earlier callers", "define replaces an earlier variable define", "define can use the old value" and "mutual recursion between top-level defines" in `test.scm`; "Unbound Variables" in `tests/test_error.c`.

---

## 7. ✅ `letrec` is a keyword but fails to parse

**Labels:** bug
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

`lexer.c` registers `letrec`, but a simple use does not compile:

```scheme
(display (letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5)))
; ERROR: 'unexpected character' ...
; ERROR: 'syntax error' ...
; ERROR: 'entity undefined' ...
```
Expected `120`. `let` and `let*` work. Possibly the same single-pass "entity undefined" problem as the self-reference inside the lambda (see the forward-reference issue), since `f` is not bound yet while its own initializer is compiled.

### Cause
`letrec` was never implemented: `expr()` has no case for the `LETREC` token, so it fell through to the syntax error. (`let*` did not work either; see #11.) Implementing it needed two things the compiler did not have:
* **Shared captured variables.** The natural desugaring is `((lambda (f ...) (set! f init) ... body) <unspecified> ...)`, but closures copied captured variables when they were created, so a lambda in `init` would have kept the placeholder instead of seeing the `set!`. Fixed by #10.
* **All names known before any init is compiled.** Mutually recursive inits refer to names that come later, and the compiler is single pass.

### Fix
`letrec_expr` records the tokens of the binding list, collects the binding names from them, and hands the tokens back to the lexer to compile (`TokenReplay`, a stack of replays inside `yylex`). That works for files, `eval` strings and the REPL alike. It then emits the desugaring above: the names become parameters of a lambda that starts with every variable unspecified, and the lambda's body prologue (`local_body_ex`) compiles each init followed by `OP_SET_LOCAL` before the body.

### Verification
The repro prints `120`. Mutually recursive `even?`/`odd?` run 100K deep, a `letrec` loop runs 100K iterations in constant stack, and nested, empty and non-lambda bindings work. Tests: the five "letrec" tests in `test.scm`.

---

## 8. Tracking: R5RS features missing or incomplete

**Labels:** enhancement

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

Checked individually on `main`. Each item below fails today.

**Binding and procedures**
- [x] variadic procedures: `(define (f . args) ...)`, `(lambda args ...)` (see #14)
- [x] `apply` (the stdlib one takes a function and a list and maps it, which is not `apply`) (see #14)
- [x] named `let`: `(let loop ((i 0)) ...)` (see #14)
- [x] `call/cc`: escape-only (see #14)
- [ ] `define-syntax` / `syntax-rules`
- [ ] quasiquote / unquote
- [ ] `when`, `unless`
- [ ] `guard` / `raise` / `with-exception-handler`

**Data**
- [ ] vectors (`make-vector`, `vector-ref`, `vector-set!`, `vector`, `#(...)`)
- [ ] hash tables
- [x] dotted-pair literals in quoted data: `'(1 . 2)` (the printer handles `(cons 1 2)` fine; this is a lexer/parser gap and currently also aborts, see the syntax-error issue) (see #14)

**Strings and characters**
- [ ] `string=?`, `string<?` and friends
- [ ] `string->list`, `list->string`
- [ ] `char-alphabetic?` and other char predicates

**Library**
- [ ] `sort`, `iota`
- [ ] `exact->inexact`
- [x] operators as values (see #14): `(map + '(1 2) '(3 4))` is a syntax error because `+` (and `-`, `*`, `<`, ...) is only accepted in call position. Multi-list `map` itself works: `(map (lambda (a b) (+ a b)) (list 1 2) (list 3 4))` gives `(4 6)`.
- [ ] n-ary `min` / `max` / `append` in `stdlib.scm` (two arguments only)

**Eval**
- [ ] `eval` takes a *string* (`(eval "(+ 1 2)")` works) but not a datum: `(eval '(+ 1 2))` gives `eval: expected a string argument`. R5RS `eval` takes an expression and an environment.

Already fine: `do`, `case`, `cond`, `let`, tail calls between equal-arity functions, `assq`/`assoc` and friends via `stdlib.scm`.

Happy to split any of these into their own issues.

**Update:** `let*` was not fine: it bound its variables in parallel, like `let`. Fixed, see #11. `letrec` (#7) and tail calls between any arities (#4) now work too. The open items above are unchanged.

**Update 2:** variadic procedures, `apply`, named `let`, escape-only `call/cc`, dotted pairs and operators as values are done, see #14. Identifiers can now contain `=` and the other R5RS identifier characters, so names like `string=?` are possible; the procedures themselves still do not exist.

---

## 9. ✅ Script runs echo the value of every top-level expression

**Labels:** enhancement
**Status:** Complete.

Found while writing `examples/metacircular.scm` and `examples/forth.scm`. Reproduced on `main` with the recent tail-call / `RET` / quote fixes applied.

`parser.c` emits `OP_PRINT_TOS` after every top-level expression, so running a script prints stray values (`true`, `false`, `2`, ...) between the program's own output, for example after each top-level call to a function and after every `(set! ...)`:

```
$ ./skeme examples/forth.scm
false
false
Forth running on Skeme
...
true
```

That is the right behaviour for the interactive REPL, but a non-interactive script run normally prints only what the program prints.

### Suggestion
Only emit `OP_PRINT_TOS` when running interactively (`g_bInteractive` / `-i`), or add a `-q` flag. The test suite and any existing scripts that rely on the echo would need a quick check. The demos work around it by running everything from one function.

### Fix
The echo is decided per top-level form, by where it was read. Forms typed at the REPL (stdin) still end in `OP_PRINT_TOS`. Forms from a script file, from a file it `load`s, or from `eval` end in the new `OP_DISCARD_TOS`, which drops the value without printing it. This also covers `skeme -i script.scm`: the script part is quiet and the REPL that follows echoes.

Most of the stray `false` lines came from `set!`, which returned `#f`. It now yields the unspecified value (#5), which the REPL does not echo either. `if` without an else branch still yields `#f`: making that unspecified would make it count as true, which could change what existing programs do.

### Verification
Checked by hand; there is no automated test because the echo goes to stdout. `(define x 1) (set! x 2) (+ 1 2) (display "hi")` as a script prints only `hi`. At the REPL the same input echoes `3` and nothing for `define`/`set!`. `test.scm` and both examples pass and print only their own output.

---

## 10. ✅ Closures copy captured variables, so they do not see later `set!`s

**Labels:** bug
**Status:** Complete. Found while working on #7.

A closure got a copy of each captured variable when it was created. So two closures over the same variable did not see each other's updates, and a closure did not see a `set!` made by its enclosing function afterwards:

```scheme
(define (mk) (let ((n 0)) (list (lambda () (set! n (+ n 1)) n) (lambda () n))))
(define fs (mk)) ((car fs)) ((car fs))
(display ((car (cdr fs))))      ; 0, should be 2

(define (f) (let ((n 0)) (let ((g (lambda () n))) (set! n 5) (g))))
(display (f))                   ; 0, should be 5
```

It also made `letrec` (#7) impossible to build on `let` + `set!`.

### Fix
Lua/clox-style upvalues. A captured variable is an `Upvalue` object shared by all the closures that capture it. While its frame is live the upvalue is open and refers to the variable's stack slot, so the frame and the closures read and write the same place. When the frame goes away (`RET`, a tail call reusing the frame, or an abort) `vm_close_upvalues` copies the value into the upvalue. The compiler now emits `OP_CAPTURE_LOCAL` / `OP_CAPTURE_UPVALUE` before `OP_MAKE_CLOSURE` instead of `OP_GET_LOCAL` / `OP_GET_UPVALUE`. The VM keeps a list of open upvalues (sorted by slot) as a GC root, and `OP_GET_UPVALUE` / `OP_SET_UPVALUE` go through the upvalue. Each tail-call iteration still gets its own variables, because the tail call closes the old frame's upvalues before reusing it.

### Verification
Both repros give the Scheme answers (`2`, `5`). Tests: the four new "closure:" tests in `test.scm` (shared variable, `set!` after creation, separate calls get separate variables, per-iteration variables across tail calls). `bench.scm` runs at the same speed as before.

---

## 11. ✅ `let*` binds in parallel; `(let () ...)` does not parse

**Labels:** bug
**Status:** Complete. Found while working on #7.

`let*` was parsed exactly like `let`, so a binding could not see the ones before it:

```scheme
(display (let* ((a 1) (b (+ a 1))) b))   ; unbound variable 'a' (it read a global), should be 2
```

Separately, `()` is lexed as a single nil token, so an empty binding list failed: `(let () 5)` was a syntax error.

### Fix
`let*` compiles to nested one-binding lets: each binding's value is compiled inside the lambda of the previous binding, which is called as a tail call. The innermost lambda compiles the body. `let`, `let*` and `letrec` accept `()` as an empty binding list.

### Verification
Tests: "let* sees earlier bindings", "let* binding shadows a global", "let* body in tail position" (100K iterations), "let with no bindings" and "let* with no bindings" in `test.scm`.

---

## 12. ✅ Objects freed while still in use (GC rooting bugs)

**Labels:** bug
**Status:** Complete. Found by building with `-DVM_STRESS_GC`, which collects on every allocation.

On `main` under GC stress, `test.scm` fails 5 tests and `examples/metacircular.scm` computes wrong answers. In normal runs a badly timed collection could do the same. The causes:
* `map` kept its partial result list rooted only around its own allocation, not while calling the mapped function, which can allocate (and collect).
* `string->symbol` and `symbol->string` popped their argument and then allocated, then read the argument's name, possibly from freed memory.
* The new `<define-N>` root (#6) was first created after allocating the name, which can collect the procedure being defined; caught by the same stress run.

### Fix
Each one keeps its objects on the VM stack, which the GC marks, across the allocation.

### Verification
Under a `-DVM_STRESS_GC` build: `test.scm` 348/348, the C unit tests pass, and both examples print exactly what a normal build prints.

### Later fixed
The GC did not trace object constants embedded in bytecode; they were safe only because something else kept them alive. It does now (#17).

---

## 13. ✅ Internal `define` creates a global

**Labels:** bug
**Status:** Fixed (working tree, not yet committed). Found while working on #7.

A `define` inside a function body binds a global, not a local of the function:

```scheme
(define (outer) (define (inner n) (if (= n 0) 0 (inner (- n 1)))) (inner 5))
(outer)
(display inner)      ; #<procedure:inner>, should be unbound
```

Recursion through the internal define works, but the name leaks to the top level, two functions with internal defines of the same name overwrite each other, and a recursive `outer` shares one `inner` between its calls.

### Fix
A body's leading internal defines compile like `letrec*` over the rest of the body, so the names are local to each call and each definition can use the ones before it:

```scheme
(define (f x) (define a 1) (define (g y) (+ a y)) (g x))
; compiles like
(define (f x) ((lambda (a g) (set! a 1) (set! g (lambda (y) (+ a y))) (g x)) <unspecified> <unspecified>))
```

* `body_defines` in `parser.c` runs at the start of every body (`local_body_ex`, after any prologue), so it covers `define`, `lambda`, `let`, `let*`, `letrec` and named `let` bodies. As in `letrec`, it reads the leading defines ahead to collect their names, then replays the tokens; its prologue, `body_define_inits`, compiles each define and assigns it to its variable. The recording code that `letrec` used is now shared: `replay_record` and `replay_start`.
* `(define (name param ...) ...)` inside a body compiles through `lambda_ex`, split out of `lambda()`, which takes the procedure's name (used in messages such as "'g' called with 1 argument").
* The inner lambda's call is in tail position, so a tail call at the end of a body with defines still runs in constant stack.
* As in R7RS, defines are allowed only at the start of a body. A `define` anywhere else inside a procedure, e.g. after an expression or inside an `if`, is now a syntax error: "define is only allowed at top level or at the start of a body". The local branch of `definition()`, which stored the inner procedure to a global under its real name, is gone.

### Cost
A procedure whose body starts with defines makes one extra call (to the inner lambda) per call, and its inner procedures that refer to each other or to themselves are closures. Procedures without internal defines are unaffected; `fib(30)` timing is unchanged.

### Verification
14 new tests in `test.scm` (411 total): the name doesn't leak, the same inner name in two procedures, a separate inner define per recursive call, mutual recursion, defines that use earlier ones, tail calls and loops in constant stack, and defines in `lambda`, `let`, `let*`, named `let` and variadic bodies. Several fail on `HEAD`. A "Define Placement" suite in `tests/test_error.c` (241 total) checks the error for a misplaced define and that parsing continues. The existing "inner define" tests pass unchanged.

---

## 14. ✅ Named let, variadic procedures, apply, call/cc, dotted pairs, interned symbols

**Labels:** enhancement
**Status:** Complete. Items from the #8 list.

### What was added
* **Interned symbols.** `vm_new_symbol` returns one object per name, from a weak `interned_symbols` table that the GC prunes like the string table. `eq?` on symbols is a pointer compare again, so the compare-by-name workaround in `eq?`/`equal?` is gone, and quoted symbols no longer allocate each time they are evaluated.
* **Dotted pairs in quoted data**, `'(1 . 2)` and `'(a b . c)`, via a new `OP_LIST_TAIL n` that builds a list onto a given tail. The `read` builtin reads dotted lists too.
* **Operators and `list` as values.** `+ - * / < > = <= >=` and `list` can be passed and stored like any procedure: `(map + a b)`, `(apply list xs)`. A new `list` native backs the name; the `(list ...)` form still compiles to `OP_LIST`.
* **More identifier characters.** Identifiers can contain the R5RS subsequent characters `/ = + . : % & ^ ~ $ @`, so `call/cc` and `string=?` are single names. The identifier buffer is now bounds-checked; before, a very long name overflowed it.
* **Variadic procedures**: `(define (f a . rest) ...)`, `(lambda args ...)`, `(lambda (a . rest) ...)`. A `Procedure` has a `has_rest` flag, and `vm_bind_args` gathers the extra arguments into a list at every call path (call, tail call, calls from C). `(lambda () ...)` no longer leaves the `()` token to be compiled as part of the body.
* **`apply`** as a native: `(apply f a ... lst)`. It spreads the list and asks the VM to make the call (`vm_request_call`), so the result is f's and an `apply` in tail position is a tail call. The old `apply` in `stdlib.scm`, which mapped f over a list, is removed, and the "apply plus1" test in `test.scm` now checks real `apply`.
* **Named let**: `(let loop ((i 0)) ...)`. It compiles to a lambda that binds `loop` to the loop procedure and makes the first call; recursive calls are ordinary tail calls, so loops run in constant stack.
* **`call/cc`** (and `call-with-current-continuation`), escape-only. A continuation can be called while its `call/cc` is still running, from any depth: from nested calls, from inside `map`, or across nested `call/cc`s. That covers early exit and exception-style returns. Calling it after its `call/cc` has returned is an error: "only escaping continuations are supported". Full re-entrant continuations (generators, coroutines) would need the stack captured and captured variables moved to the heap.

### Related fixes
* **Wrong argument counts abort.** A call with the wrong number of arguments is a runtime error that unwinds (through `vm_abort`), and the message names the procedure: `'f' called with 1 argument, expects 2`. Before, it printed an error, left no result, and skipped a word of the next instruction (`ip++`). Calling a non-procedure also aborts.
* **Native nesting is limited.** Natives that call back into Scheme (`map`, `call/cc`, `apply` from C) nest C calls, and deep recursion through them crashed the process. A Debug build crashed between 700 and 1500 levels, Release between 1500 and 2000. They now stop at `VM_MAX_NATIVE_DEPTH` (500, configurable) with an error.
* **Abandoned frames close their upvalues.** When an abort or escape abandons frames inside `vm_call_function`, their upvalues are now closed.
* **`AS_NUM` double evaluation.** `AS_NUM` evaluates its argument twice, so `AS_NUM(vm_pop(...))` pops two values. Fixed in `op_pop_fp`/`op_pop_sp`; the new natives pop into a variable first.

### Verification
38 new tests in `test.scm` (386 total) under "language features", and a "Call Errors" suite in `tests/test_error.c` (230 total): arity messages, stale continuations, the nesting limit and recovery. Everything passes in Debug, Release and a `-DVM_STRESS_GC` build, plus `ctest`.

---

## 15. ✅ Small bugs noticed along the way

**Labels:** bug
**Status:** Fixed (working tree, not yet committed).

* ✅ `(- 5)` returned `5`; it now negates. Native arities below zero (`SK_ARITY_ONE_OR_MORE` and the like) are never checked, so the arithmetic natives handle short calls themselves:
  * `(- x)` negates.
  * `(/ x)` is `(/ 1 x)`, with the usual integer division, so `(/ 2)` is `0` and `(/ 2.0)` is `0.5`.
  * `(+)` is `0` and `(*)` is `1`. Before, they read past their arguments on the stack.
  * `(-)` and `(/)` are errors.
* ✅ The empty list printed as `nil`; it now prints as `()`, e.g. `(1 () 2)`. `PRINT_NIL` in `vm.h`. No test or example depended on the old text. `nil` is still accepted as a name for the empty list in source.
* ✅ `.5` was not read as a number; `.5`, `-.5` and `+.25` now are. A `.` followed by a digit starts a number; a lone `.` is still the dotted-pair token, so `'(a . 5)` and `'(a .5)` differ as they should. A sign and dot with no digits, `-.`, is a "malformed number" error.
* ✅ A top-level `(define (f) ...)` binds `f` while it is being compiled, so its body can call it. If the form then had a syntax error and was dropped, `f` stayed bound to the half-compiled procedure. Fixed in #16.

Tests: 11 new in `test.scm` under "arithmetic with one or zero arguments" and "numbers starting with a dot".

---

## 16. ✅ Reading code from a string: `eval` hangs, `load` inside `eval`, dropped defines

**Labels:** bug
**Status:** Fixed (working tree, not yet committed). The first two were reported by a review; the rest turned up while fixing them.

### Problems
* **`(eval "x")` hung.** `getch` returns EOF at the end of an eval string without advancing, but `ungetch` stepped back anyway. A symbol or number at the very end of the string was then re-read forever (`"x"`), or re-read as an extra form: `(eval "42")` returned `2`. File input was unaffected, because `ungetc(EOF)` does nothing.
* **`load` inside `eval` did nothing.** `getch` always read the eval string while there was one, so a file that `load` pushed onto the input stack was never read. It also stayed on the stack, so later error messages named the wrong file.
* **The eval string could be freed while it was read.** The `eval` builtin popped its string argument before parsing it in place. A collection during compiling (for example, while loading a file) could free it, and the lexer then read freed memory. The GC-stress build shows this.
* **`eval` of several forms** left every form's value on the stack, not just the last one, so a script ending in such an `eval` printed leftover values.
* **A dropped top-level procedure define stayed bound** (from #15): a form with a syntax error is discarded, but the compile-time binding of its name was not undone.

### Fix
* `ungetch` ignores EOF (`lexer.c`).
* `SKState.eval_file_depth` records the input-stack depth an eval string belongs to (set by `sk_eval_string` and by the `do` step code in `parser.c`, which also lexes from strings). The lexer reads the string only at that depth, so a file loaded from it is read first. At the end of that file the lexer pops back to the string, even when that leaves only the sentinel entry (as it does at run time, after a script has been compiled). `sk_eval_string` closes any loaded files that parsing stopped inside.
* `sk_eval` keeps its string on the stack while it is parsed, and returns the last form's value, dropping everything else the string left on the stack.
* `definition()` saves the name's previous value under the hidden global `<define-prev>` (so the GC sees it) and records the name in `SKState.define_name`. After each top-level form, `finish_define` in `yyparse` puts the previous value back if the form was dropped, then releases the saved value.

### Verification
4 new tests in `test.scm` (415 total): `eval` of a bare symbol, a bare number, several forms, and a string that loads `tests/eval_load.scm` (a new fixture) and then goes on to use it. An "Eval Input" suite in `tests/test_error.c` calls `sk_eval_string` directly with the same cases, and "Define Placement" now checks that a dropped define, or redefinition, leaves the name as it was (257 total). Everything passes in Debug, Release and the GC-stress build, plus `ctest`.

---

## 17. ✅ Macro traps, bytecode GC roots, REPL echo, eval error locations, native arity

**Labels:** bug, performance
**Status:** Fixed (working tree, not yet committed). Covers the remaining open items except #8, plus problems reported by reviewers.

### Value macros evaluated their argument more than once
`AS_NUM`, `IS_NUM`, `OBJ_TYPE`, `IS_FLOAT`, `AS_DOUBLE` and most other `AS_*`/`IS_*` macros in `vm.h` used their argument two or three times, so `AS_NUM(vm_pop(pvm))` popped twice. That caused a "stack underflow" in an embedder's `exit` override; `sk_exit` only avoided it by popping into a variable first.
* Each macro now passes its argument once to a `static inline` function (`sk_is_obj`, `sk_as_num`, `sk_obj_type`, `sk_is_float`, `sk_as_double`, ...). The macro names and every call site are unchanged. The object-type enum moved above them.
* A search for side-effecting arguments found only `IS_FALSE(vm_pop(...))`, which was already safe, and `get_global` in the C tests, which ran twice per check. Release speed is unchanged.

### The GC did not trace bytecode, and the REPL echoed a stray procedure
`(define (make-adder n) (lambda (x) (+ x n)))` at the REPL printed `#<procedure:<lambda-0>>`, while `(define (f x) x)` printed nothing. To keep procedures alive while compiling, the compiler left each `lambda`/`let`/`do` procedure on the VM stack, intending the stack to be cleared before running. The script path cleared it, but the REPL did not, so `OP_PRINT_TOS` echoed it. The compiler also stored every compiled procedure under a hidden global (`<lambda-N>`, `<let-N>`, `<do-N>`, `<define-N>`), so none was ever freed, not even ones compiled by `eval` in a loop.

Both were workarounds for the GC not tracing the objects bytecode refers to (#12). Fixed at the root:
* `markChunk` walks a chunk's instructions, using `op_operand_words` for their lengths, and marks pushed constants, called procedures and global keys. A procedure's chunk is traced when the procedure is marked. A `LOAD_GLOBAL` cache is not traced: it may be stale, and a valid one is the global's current value anyway.
* New GC roots in `VM`: `root_chunks`, the top-level code being compiled or run (`main.c` registers the REPL chunk, `sk_eval_string` each eval chunk), and `compile_roots`, procedures still being compiled, until the enclosing code refers to them.
* The stack leftovers and the hidden globals are gone. Compiled procedures are now collected when unused.
* `freeObject` (and `vm_free_chunk`) free the C strings that `OP_STRING`/`OP_SYMBOL` operands own; they leaked before.
* Tracing exposed a latent bug: `vm_emit` grew a chunk with `vm_alloc`, which `realloc`s and then may collect before the caller stores the new pointer, so the GC read a freed buffer. It now allocates the new buffer, copies, and frees the old one.

### Errors from `sk_eval_string` named the wrong place
They reported the lexer's position in whatever file was on top of the input stack (e.g. `in file '<unknown>' near line 57`), not the line in the string.
* While the lexer reads an eval string, errors now report `<eval>` and the line within the string (`SKState.eval_name`, `eval_lineno`; `sk_log` checks `sk_reading_string`). The `do` loop's step code, which the parser also re-lexes from a string, keeps naming the enclosing file.
* A newline inside an eval string no longer moves the file's line count, and at the REPL it no longer ends the input: `(eval "(define a 1)\n(define b 2)")` typed at the REPL stopped after the first line.
* `eval` of a `define` (no value) now returns the unspecified value instead of `#f`, so the REPL doesn't echo `false`.

### Native argument counts were not checked unless fixed
`SK_ARITY_ONE_OR_MORE`, `TWO_OR_MORE` and `ZERO_OR_ONE` were never enforced, and a wrong fixed count only logged "incorrect number of arguments to native function" and carried on.
* `vm_run_native` checks every arity, and a wrong count aborts like a procedure's, naming the native: `'car' called with 2 arguments, expects 1`, `'map' called with 1 argument, expects at least 2`, `... expects at most 1`. The duplicate check in `vm_call_function` is gone.
* The `SK_ARITY_*` constants moved from `parser.h` to `vm.h`, beside `NativeFunction`.
* `string-append` takes any number of strings, as in Scheme (`(string-append)` is `""`). It also no longer overflows its buffer: it `strcat`ed into a fixed array with no bound.

### Inline arithmetic did not check its operands
Two-argument `+ - * < <= =` compile to opcodes whose slow path treated any non-number as 0, so `(+ 1 "x")` was `1`. They now abort with "arguments to '+' must be numbers", like the builtins.

### Stack checks per call instead of per push (from #3): tried, reverted
A Lua-style version was implemented (commit `bdff48d`) and then reverted. It computed each procedure's maximum stack depth from its bytecode, checked once per call (and once per top-level form, with a new `OP_CHECK_STACK`), and let the opcode handlers push unchecked.

It gained only about 2% on `fib(34)` (2487 vs 2533 ms, min of 9 interleaved runs, with noise of about ±3%), and nothing on `bench.scm`; the ~15% measured in #3 was against an older VM. That did not justify a stack-effect table every new opcode must keep in sync, where an underestimate would corrupt memory in Release builds. `vm_push` keeps its per-push check. The overflow tests written for it stay: deep recursion with wide frames, and a single top-level form too big for the stack.

### Verification
New C tests in `tests/test_error.c` (295 total): native arity messages, `<eval>` locations, inline arithmetic type errors, a "Compiled Code Roots" suite (nothing left on the stack after compiling a lambda, a procedure reachable only from another's code survives a collection, procedures compiled by `eval` are collected), and stack overflow with wide frames and with one oversized top-level form. One new `test.scm` test (416 total) for `string-append` with zero or one strings. Everything passes in Debug, Release and the GC-stress build, plus `ctest` in Debug and Release.
