# Skeme issues to file

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
- [x] quasiquote / unquote (see #20)
- [x] `when`, `unless` (see #18)
- [x] `guard` / `raise` / `with-exception-handler` (see #21)

**Data**
- [x] vectors (`make-vector`, `vector-ref`, `vector-set!`, `vector`, `#(...)`) (see #22)
- [x] hash tables (see #23)
- [x] dotted-pair literals in quoted data: `'(1 . 2)` (the printer handles `(cons 1 2)` fine; this is a lexer/parser gap and currently also aborts, see the syntax-error issue) (see #14)

**Strings and characters**
- [x] `string=?`, `string<?` and friends (see #18)
- [x] `string->list`, `list->string` (see #18)
- [x] `char-alphabetic?` and other char predicates (see #18)

**Library**
- [x] `sort`, `iota` (see #18)
- [x] `exact->inexact` (see #18)
- [x] operators as values (see #14): `(map + '(1 2) '(3 4))` is a syntax error because `+` (and `-`, `*`, `<`, ...) is only accepted in call position. Multi-list `map` itself works: `(map (lambda (a b) (+ a b)) (list 1 2) (list 3 4))` gives `(4 6)`.
- [x] n-ary `min` / `max` / `append` in `stdlib.scm` (two arguments only) (see #18)

**Eval**
- [x] (see #20) `eval` takes a *string* (`(eval "(+ 1 2)")` works) but not a datum: `(eval '(+ 1 2))` gives `eval: expected a string argument`. R5RS `eval` takes an expression and an environment.

Already fine: `do`, `case`, `cond`, `let`, tail calls between equal-arity functions, `assq`/`assoc` and friends via `stdlib.scm`.

Happy to split any of these into their own issues.

**Update:** `let*` was not fine: it bound its variables in parallel, like `let`. Fixed, see #11. `letrec` (#7) and tail calls between any arities (#4) now work too. The open items above are unchanged.

**Update 2:** variadic procedures, `apply`, named `let`, escape-only `call/cc`, dotted pairs and operators as values are done, see #14. Identifiers can now contain `=` and the other R5RS identifier characters, so names like `string=?` are possible; the procedures themselves still do not exist.

