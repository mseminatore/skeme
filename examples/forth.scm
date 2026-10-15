; forth.scm - a tiny Forth written in Skeme.
;
; Supports: integer literals, + - * / = < >, dup drop swap over . cr,
; colon definitions (: name ... ;), recursion, and if / else / then.
; Truth values follow Forth: -1 is true, 0 is false.
;
; Run:  ./skeme examples/forth.scm
;
; Style notes (Skeme quirks this file works around):
;  * Skeme compiles in a single pass: a global must exist before anything
;    refers to it, so helpers come first and exec-tokens is forward-declared.
;  * Skeme's tail calls only reuse the frame when caller and callee take the
;    same number of arguments. Every function in the interpreter's main loop
;    (exec-tokens, exec-token, step-*) therefore takes exactly one argument,
;    the remaining token list, so a Forth program of any length runs in
;    constant stack. Only Forth word *calls* nest.
;  * No `let`: a Skeme closure that calls something and then reads a captured
;    variable loses its upvalues. Global state plus small helpers avoids it.

(load "stdlib.scm")

; ---- tokenizer -------------------------------------------------------------
(define (whitespace? code) (< code 33))

(define (skip-ws s i n)
  (if (< i n)
      (if (whitespace? (char->integer (string-ref s i)))
          (skip-ws s (+ i 1) n)
          i)
      i))

(define (word-end s i n)
  (if (< i n)
      (if (whitespace? (char->integer (string-ref s i)))
          i
          (word-end s (+ i 1) n))
      i))

(define tokenize-from #f)

(define (tokenize-word s start n)
  (if (< start n)
      (cons (substring s start (word-end s start n))
            (tokenize-from s (word-end s start n) n))
      (quote ())))

(set! tokenize-from (lambda (s i n)
  (tokenize-word s (skip-ws s i n) n)))

(define (tokenize s)
  (tokenize-from s 0 (string-length s)))

; ---- numbers ---------------------------------------------------------------
(define (digit-code? c) (if (> c 47) (< c 58) #f))

(define (number-token? w)
  (or (digit-code? (char->integer (string-ref w 0)))
      (and (> (string-length w) 1)
           (= (char->integer (string-ref w 0)) 45)
           (digit-code? (char->integer (string-ref w 1))))))

; ---- the data stack (a global list, top first) -----------------------------
(define fstack (quote ()))

(define (push! x)
  (set! fstack (cons x fstack)))

(define (pop-aux top rest)
  (set! fstack rest)
  top)

(define (pop!)
  (if (null? fstack)
      (error "stack underflow")
      (pop-aux (car fstack) (cdr fstack))))

; ---- primitive words -------------------------------------------------------
(define (flag b) (if b -1 0))

(define (compute name a b)
  (cond ((equal? name "+") (+ a b))
        ((equal? name "-") (- a b))
        ((equal? name "*") (* a b))
        ((equal? name "/") (/ a b))
        ((equal? name "=") (flag (= a b)))
        ((equal? name "<") (flag (< a b)))
        (else (flag (> a b)))))

(define (binary-apply name b)
  (push! (compute name (pop!) b)))

(define (dup-aux x) (push! x) (push! x))
(define (swap-two b a) (push! b) (push! a))
(define (swap-top b) (swap-two b (pop!)))
(define (over-two b a) (push! a) (push! b) (push! a))
(define (over-top b) (over-two b (pop!)))
(define (print-top x) (display x) (display " "))

(define prim-names
  (list "+" "-" "*" "/" "=" "<" ">" "dup" "drop" "swap" "over" "." "cr"))

(define (run-prim w)
  (cond ((equal? w "dup")  (dup-aux (pop!)))
        ((equal? w "drop") (pop!))
        ((equal? w "swap") (swap-top (pop!)))
        ((equal? w "over") (over-top (pop!)))
        ((equal? w ".")    (print-top (pop!)))
        ((equal? w "cr")   (newline))
        (else (binary-apply w (pop!)))))

; ---- the dictionary of user-defined words ----------------------------------
(define fdict (quote ()))

(define (add-word! name body)
  (set! fdict (cons (cons name body) fdict)))

; ---- skipping over unexecuted branches -------------------------------------
; skip-false: the flag was false. Return the tokens after the matching else
; (run the false branch) or, if there is none, after the matching then.
(define (skip-false toks depth)
  (cond ((null? toks) (error "if without then"))
        ((equal? (car toks) "if")   (skip-false (cdr toks) (+ depth 1)))
        ((equal? (car toks) "then")
         (if (= depth 0) (cdr toks) (skip-false (cdr toks) (- depth 1))))
        ((equal? (car toks) "else")
         (if (= depth 0) (cdr toks) (skip-false (cdr toks) depth)))
        (else (skip-false (cdr toks) depth))))

; skip-true: we finished a true branch and hit else. Skip to after the then.
(define (skip-true toks depth)
  (cond ((null? toks) (error "else without then"))
        ((equal? (car toks) "if")   (skip-true (cdr toks) (+ depth 1)))
        ((equal? (car toks) "then")
         (if (= depth 0) (cdr toks) (skip-true (cdr toks) (- depth 1))))
        (else (skip-true (cdr toks) depth))))

; ---- colon definitions -----------------------------------------------------
(define (body-until-semi toks)
  (if (null? toks)
      (error "definition without ;")
      (if (equal? (car toks) ";")
          (quote ())
          (cons (car toks) (body-until-semi (cdr toks))))))

(define (after-semi toks)
  (if (null? toks)
      (quote ())
      (if (equal? (car toks) ";")
          (cdr toks)
          (after-semi (cdr toks)))))

; ---- the interpreter -------------------------------------------------------
(define exec-tokens #f)

(define (exec-user w entry)
  (if entry
      (exec-tokens (cdr entry))
      (error "unknown word" w)))

(define (exec-word w)
  (if (member w prim-names)
      (run-prim w)
      (exec-user w (assoc w fdict))))

(define (step-word toks)
  (exec-word (car toks))
  (exec-tokens (cdr toks)))

(define (step-number toks)
  (push! (string->number (car toks)))
  (exec-tokens (cdr toks)))

(define (step-if toks)
  (if (= (pop!) 0)
      (exec-tokens (skip-false (cdr toks) 0))
      (exec-tokens (cdr toks))))

(define (step-else toks)
  (exec-tokens (skip-true (cdr toks) 0)))

(define (step-then toks)
  (exec-tokens (cdr toks)))

(define (step-define toks)
  (add-word! (cadr toks) (body-until-semi (cddr toks)))
  (exec-tokens (after-semi (cddr toks))))

(define (exec-token toks)
  (cond ((equal? (car toks) ":")    (step-define toks))
        ((equal? (car toks) "if")   (step-if toks))
        ((equal? (car toks) "else") (step-else toks))
        ((equal? (car toks) "then") (step-then toks))
        ((number-token? (car toks)) (step-number toks))
        (else (step-word toks))))

(set! exec-tokens (lambda (toks)
  (if (null? toks)
      #t
      (exec-token toks))))

(define (forth src)
  (exec-tokens (tokenize src)))

; ---- demo ------------------------------------------------------------------
(define (try src)
  (display "forth> ")
  (display src)
  (newline)
  (display "  ")
  (forth src)
  (newline))

; Everything runs from one function so the script prints a single
; top-level result instead of echoing every expression.
(define (demo)
  (display "Forth running on Skeme")
  (newline)

  (try "2 3 + .")
  (try "10 3 - 4 * .")
  (try ": square dup * ;")
  (try "7 square .")
  (try ": cube dup square * ;")
  (try "3 cube .")
  (try "1 2 swap . .")
  (try "1 2 over . . .")
  (try ": fact dup 1 > if dup 1 - fact * then ;")
  (try "6 fact .")
  (try ": fib dup 2 < if else dup 1 - fib swap 2 - fib + then ;")
  (try "10 fib .")
  (try ": countdown dup . dup 0 > if 1 - countdown then ;")
  (try "5 countdown drop")
  (try ": sign dup 0 < if drop -1 else 0 > if 1 else 0 then then . ;")
  (try "-5 sign")
  (try "7 sign")
  (try "0 sign")
  #t)

(demo)
