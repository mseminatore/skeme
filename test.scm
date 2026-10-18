;-----------------------------------------------
; Skeme test cases
;
; Copyright (C) 2026 by Mark Seminatore
;-----------------------------------------------

(load "stdlib.scm")
(load "skunit.scm")

;(load "core.scm")
;(load "io.scm")
;(load "../fib.scm")

;-----------------------
; enable logging options
;-----------------------

; turn on VM logging
;(vmlog)

; turn on parser logging
;(parselog)

; turn on GC logging
;(gclog)

;----------------------
; test map (native implementation)
;----------------------
(test-case "map with number?" (equal? (map number? (list 1 2 3)) (list true true true)))
(test-case "map with car" (equal? (map car (list (list 1 2) (list 3 4))) (list 1 3)))
(test-case "map with cdr" (equal? (map cdr (list (list 1 2) (list 3 4))) (list (list 2) (list 4))))
(test-case "map empty list" (null? (map number? (list))))

;----------------------
; test apply
;----------------------
;(test-suite "apply")
(define (plus1 a) (+ 1 a))
(test-case "apply plus1" (= (apply plus1 (list 1)) 2))

;----------------------
; test filter
;----------------------
(define (one? n) (= 1 n))
(test-case "length of filter list" (= 3 (length (filter one? (list 1 2 1 1)))))
(test-case "equal of filter list" (equal? (filter one? (list 1 2 1 1)) (list 1 1 1)))

;----------------------
; test even?
;----------------------
(test-case "even? test" (and (even? 2) (not (even? 3))))

;----------------------
; test odd?
;----------------------
(test-case "odd? test" (and (odd? 3) (not (odd? 2))))

;----------------------
; test min/max
;----------------------
(test-case "min test" (= 1 (min 1 2)))
(test-case "max test" (= 2 (max 1 2)))

;----------------------
; test variable define and set!
;----------------------
(define five 5)

(test-case "five = 5" (= five 5))
(set! five 4)
(test-case "set five to 4" (= 4 five))
(set! five 5)

; test set! on local variables
(define (set-local-test x)
  (set! x (+ x 10))
  x)
(test-case "set! local" (= (set-local-test 5) 15))

(define (set-local-begin x)
  (begin (set! x 99) x))
(test-case "set! local in begin" (= (set-local-begin 1) 99))

(define (set-two a b)
  (set! a (* a 2))
  (set! b (* b 3))
  (+ a b))
(test-case "set! two locals" (= (set-two 5 10) 40))

;----------------------
; let / let* bindings
;----------------------
(test-case "let basic" (= (let ((x 10) (y 20)) (+ x y)) 30))
(test-case "let single" (= (let ((x 42)) x) 42))
(test-case "let body expr" (= (let ((a 3) (b 4)) (* a b)) 12))
(test-case "let nested" (= (let ((x 5)) (let ((y 10)) (+ x y))) 15))

(define (make-let-adder n)
  (let ((offset n))
    (lambda (x) (+ offset x))))
(define la (make-let-adder 100))
(test-case "let closure capture" (= (la 5) 105))

(test-case "let* basic" (= (let* ((x 10) (y 20)) (+ x y)) 30))
(test-case "let* single" (= (let* ((x 42)) x) 42))

;----------------------
; construct a pair
;----------------------
(test-case "cons a pair" (cons 1 2))
(define apair (cons 1 2))
(test-case "pair? a pair" (pair? (cons 1 2)))
(test-case "pair? a pair var" (pair? apair))

;----------------------
; make list via nested cons
;----------------------
(test-case "nested cons" (cons 1 (cons 2 ())))
(test-case "list? nested cons list" (list? (cons 1 (cons 2 ()))))

;----------------------
; make a list
;----------------------
(test-case "make a list" (list 1 2 3))
(define alist (list 1 2 3))
(test-case "list? list var "(list? alist))
(test-case "car of list" (= (car alist) 1))
(test-case "cdr of list" (list? (cdr alist)))
(test-case "cadr of list" (= (cadr alist) 2))
(test-case "cddr of list" (list? (cddr alist)))
(test-case "cddr length" (= (length (cddr alist)) 1))
(test-case "cddr value" (= (car (cddr alist)) 3))

;----------------------
; set car and cdr
;----------------------
;(set-car! apair 10)
(test-case "set-car! apair to 10" (= (car (set-car! apair 10)) 10))
;(set-cdr! apair 20)
(test-case "set-cdr! apair to 20" (= (cdr (set-cdr! apair 20)) 20))

; set back to original values
(test-case "set-car! apair back to 1" (= (car (set-car! apair 1)) 1))
(test-case "set-cdr! apair back to 2" (= (cdr (set-cdr! apair 2)) 2))

;----------------------
; typeof tests
;----------------------
(test-case "typeof number" (eq? "number" (typeof 42)))
(test-case "typeof string" (eq? "string" (typeof "hello")))
(test-case "typeof boolean" (eq? "boolean" (typeof #t)))
(test-case "typeof pair" (eq? "pair" (typeof apair)))
(test-case "typeof list" (eq? "list" (typeof alist)))

;----------------------
; test append
;----------------------
(test-case "append two lists" (equal? (append (list 1 2) (list 3 4)) (list 1 2 3 4)))

;----------------------
; test map
;----------------------
(test-case "map plus1 over list" (equal? (map plus1 (list 1 2 3)) (list 2 3 4)))

;----------------------
; reverse a list
;----------------------
(test-case "reverse a list" (equal? (reverse (list 1 2 3)) (list 3 2 1)))
(test-case "reverse empty list" (equal? (reverse ()) ()))

;----------------------
; characterization operations
;----------------------
(test-case "zero? 0" (zero? 0))
(test-case "zero? 1" (not (zero? 1)))
(test-case "empty list check ()" (null? ()))
(test-case "empty list check '()" (null? '()))
(test-case "empty list check nil" (null? nil))
(test-case "number? 1" (number? 1))
(test-case "number? #t" (not (number? #t)))
(test-case "string? foo" (string? "foo"))
(test-case "atom? 1" (atom? 1))
(test-case "atom? five" (atom? five))
(test-case "atom? on a list" (not (atom? alist)))
(test-case "atom? on a pair" (not (atom? apair)))
(test-case "length string" (= (length "foo") 3))
(test-case "length list" (= (length (list 1 2 3 4 5)) 5))
(test-case "foo = foo" (eq? "foo" "foo"))
(test-case "foo != bar" (not (eq? "foo" "bar")))
(test-case "integer? 1" (integer? 1))
(test-case "integer? #t" (not (integer? #t)))
;(assert (not (integer? 1.5)) "integer? failed")

;----------------------
; file operations
;----------------------
(define out (open-output-file "test.tmp"))
(test-case "open output file" (eq? "file" (typeof out)))
(test-case "write-line" (write-line "testing123" out))
(test-case "close output file" (close-file out))

(define in (open-input-file "test.tmp"))
(test-case "open input file" (eq? "file" (typeof in)))
(test-case "read-line" (eq? "testing123" (read-line in)))
(test-case "close input file" (close-file in))

; cleanup
(test-case "delete file" (delete-file "test.tmp"))

;----------------------
; read tests
;----------------------
(define rw (open-output-file "read_test.tmp"))
(write-line "42" rw)
(write-line "\"hello\"" rw)
(write-line "(1 2 3)" rw)
(write-line "#t" rw)
(write-line "foo" rw)
(close-file rw)

(define rr (open-input-file "read_test.tmp"))
(test-case "read number" (= 42 (read rr)))
(test-case "read string" (equal? "hello" (read rr)))
(test-case "read list"   (equal? (list 1 2 3) (read rr)))
(test-case "read bool"   (eq? #t (read rr)))
(test-case "read symbol" (equal? "foo" (symbol->string (read rr))))
(close-file rr)
(delete-file "read_test.tmp")

;----------------------
; printf tests
;----------------------
(test-case "printf basic"     (printf ""))
(test-case "printf ~a"        (printf "~a" 42))
(test-case "printf ~s"        (printf "~s" "hi"))
(test-case "printf ~%"        (printf "~%"))
(test-case "printf ~~"        (printf "~~"))
(test-case "printf multi-arg" (printf "~a ~a" 1 2))

;----------------------
; conversion operations
;----------------------
(test-case "number->string" (eq? "32" (number->string 32)))
(test-case "string->number" (= 32 (string->number "32")))

;----------------------
; logical operations
;----------------------
(test-case "boolean? #t" (boolean? #t))
(test-case "boolean? #f" (boolean? #f))
(test-case "boolean? 1" (not (boolean? 1)))
(test-case "and test" (and 1 2 3))
(test-case "or test" (or 1 1 1))
(test-case "not test" (not #f))

;----------------------
; comparison operations
;----------------------
(test-case "if test" (if (< 1 2) #t))
(test-case "if test" (if (> 1 2) #f #t))
(test-case "cond test" (cond ((< 1 2) #t)))
(test-case "cond else test" (cond ((> 1 2) #f) (else #t)))

;----------------------
; arithmetic operations
;----------------------
(test-case "1 + 2" (= (+ 1 2) 3))
(test-case "5 - 2" (= (- 5 2) 3))
(test-case "2 * 3" (= (* 2 3) 6))
(test-case "6 / 2" (= (/ 6 2) 3))
(test-case "7 modulo 3" (= (modulo 7 3) 1))

;----------------------
; define a function
;----------------------
(define (add a b)
    (+ a b))

(test-case "add(2,3)" (= (add 2 3) 5))
(test-case "add(-1,1)" (= (add -1 1) 0))
(test-case "add(0,0)" (= (add 0 0) 0))
(test-case "add(100,200)" (= (add 100 200) 300))

;----------------------
; recursive function
;----------------------
(define (factorial n)
    (if (zero? n)
        1
        (* n (factorial (- n 1)))))
        
(test-case "factorial(5)" (= (factorial 5) 120))
(test-case "factorial(0)" (= (factorial 0) 1))
(test-case "factorial(1)" (= (factorial 1) 1))
(test-case "factorial(6)" (= (factorial 6) 720))
(test-case "factorial(10)" (= (factorial 10) 3628800))

;----------------------
; test fibonacci function
;----------------------
(define (fibonacci n)
    (if (<= n 1)
        n
        (+ (fibonacci (- n 1)) (fibonacci (- n 2)))))

(test-case "fibonacci(10)" (= (fibonacci 10) 55))
(test-case "fibonacci(0)" (= (fibonacci 0) 0))
(test-case "fibonacci(1)" (= (fibonacci 1) 1))
(test-case "fibonacci(5)" (= (fibonacci 5) 5))
(test-case "fibonacci(15)" (= (fibonacci 15) 610))
(test-case "fibonacci(20)" (= (fibonacci 20) 6765))
; NOTE: fibonacci(25) and (30) are extremely slow with naive recursion
; Uncomment to stress test, but expect long runtimes
; (test-case "fibonacci(25)" (= (fibonacci 25) 75025))
; (test-case "fibonacci(30)" (= (fibonacci 30) 832040))

;----------------------
; test tail-recursive fibonacci
;----------------------
(define (fib-iter a b count)
  (if (= count 0)
    b
    (fib-iter (+ a b) a (- count 1))))

(define (fib n)
  (fib-iter 1 0 n))

(test-case "tail-recursive fib(10)" (= (fib 10) 55))
(test-case "tail-recursive fib(0)" (= (fib 0) 0))
(test-case "tail-recursive fib(1)" (= (fib 1) 1))
(test-case "tail-recursive fib(5)" (= (fib 5) 5))
(test-case "tail-recursive fib(15)" (= (fib 15) 610))
(test-case "tail-recursive fib(20)" (= (fib 20) 6765))

;----------------------
; passed param test
;----------------------
(define (bind1 f x) (f x))
(define (sqr x) (* x x))
(test-case "passed fn param" (= (bind1 sqr 5) 25))

;----------------------
; test lambda expressions
;----------------------
(define double (lambda (x) (* x 2)))
(test-case "lambda with define" (= (double 5) 10))

(define add-lambda (lambda (a b) (+ a b)))
(test-case "lambda two args" (= (add-lambda 3 4) 7))

; Note: passing lambdas directly as arguments
(test-case "pass lambda to fn" (= (bind1 (lambda (n) (+ n 1)) 10) 11))

;----------------------
; test character literals and predicates
;----------------------
(test-case "char? on char literal" (char? 'a'))
(test-case "char? on integer" (not (char? 42)))
(test-case "char? on string" (not (char? "hello")))
(test-case "char->integer 'a'" (= (char->integer 'a') 97))
(test-case "char->integer 'A'" (= (char->integer 'A') 65))
(test-case "integer->char 65" (char? (integer->char 65)))
(test-case "char roundtrip" (= (char->integer (integer->char 97)) 97))

;----------------------
; test Scheme-style character literals
;----------------------
(test-case "Scheme char #\\a" (char? #\a))
(test-case "Scheme char #\\A" (char? #\A))
(test-case "Scheme char #\\newline" (= (char->integer #\newline) 10))
(test-case "Scheme char #\\space" (= (char->integer #\space) 32))
(test-case "Scheme char #\\tab" (= (char->integer #\tab) 9))
(test-case "Scheme char #\\return" (= (char->integer #\return) 13))

;----------------------
; test float literals and predicates
;----------------------
(test-case "float? on float literal" (float? 3.14))
(test-case "float? on integer" (not (float? 42)))
(test-case "float? on negative float" (float? -2.5))
(test-case "float? on scientific notation" (float? 1e10))

;----------------------
; test float conversion functions
;----------------------
(test-case "string->float valid" (float? (string->float "3.14")))
(test-case "string->float negative" (float? (string->float "-2.5")))
(test-case "string->float scientific" (float? (string->float "1e10")))
(test-case "string->float invalid" (not (string->float "invalid")))
(test-case "float->string 3.14" (string? (float->string 3.14)))
(test-case "float->string roundtrip" (float? (string->float (float->string 3.14))))

;----------------------
; test float arithmetic
;----------------------
(test-suite "float math")

(test-case "float + float" (= (+ 1.5 2.5) 4.0))
(test-case "int + float" (= (+ 1 2.5) 3.5))
(test-case "float - float" (= (- 5.0 2.0) 3.0))
(test-case "float * float" (= (* 2.0 3.0) 6.0))
(test-case "float / float" (= (/ 10.0 4.0) 2.5))
(test-case "float < float" (< 1.5 2.5))
(test-case "float > int" (> 3.14 3))
(test-case "float <= int" (<= 3.0 3))
(test-case "float >= float" (>= 3.14 3.14))

;----------------------
; test multi-list map
;----------------------
(define (add2 a b) (+ a b))
(define (mul2 a b) (* a b))
(test-case "map add two lists" (equal? (map add2 (list 1 2 3) (list 10 20 30)) (list 11 22 33)))
(test-case "map mul two lists" (equal? (map mul2 (list 2 3 4) (list 5 6 7)) (list 10 18 28)))

;----------------------
; test begin expression
;----------------------
(test-suite "being")

(test-case "begin returns last" (= (begin 1 2 3) 3))
(test-case "begin single expr" (= (begin 42) 42))
(test-case "begin with exprs" (= (begin (+ 1 2) (* 3 4)) 12))

;----------------------
; test case expression
;----------------------
(test-suite "case")

(test-case "case match first" (= (case 1 ((1) 10) ((2) 20)) 10))
(test-case "case match second" (= (case 2 ((1) 10) ((2) 20)) 20))
(test-case "case else" (= (case 3 ((1) 10) ((2) 20) (else 30)) 30))
(test-case "case multi-datum" (= (case 2 ((1 2 3) 10) (else 20)) 10))


;----------------------
; test closures
;----------------------
(test-suite "closures")

(define (make-adder n) (lambda (x) (+ x n)))
(define add5 (make-adder 5))
(define add10 (make-adder 10))
(test-case "closure make-adder 5" (= (add5 10) 15))
(test-case "closure make-adder 10" (= (add10 3) 13))
(test-case "closure independent" (= (+ (add5 1) (add10 1)) 17))

(define (make-pair a b) (lambda (sel) (if sel a b)))
(define p (make-pair 3 7))
(test-case "closure pair first" (= (p #t) 3))
(test-case "closure pair second" (= (p #f) 7))

; closure capturing multiple variables
(define (make-linear a b) (lambda (x) (+ (* a x) b)))
(define line (make-linear 3 7))
(test-case "closure multi-capture" (= (line 10) 37))

; mutable closure (counter)
(define (make-counter)
  (define count 0)
  (lambda ()
    (set! count (+ count 1))
    count))
(define counter (make-counter))
(test-case "mutable closure 1" (= (counter) 1))
(test-case "mutable closure 2" (= (counter) 2))
(test-case "mutable closure 3" (= (counter) 3))

; two closures sharing mutable state
(define (make-shared)
  (define x 0)
  (list
    (lambda () (set! x (+ x 1)) x)
    (lambda () x)))
(define shared (make-shared))
(define inc! (car shared))
(define get-val (cadr shared))
(inc!)
(test-case "shared closure mutation" (= (get-val) 1))
(inc!)
(test-case "shared closure mutation 2" (= (get-val) 2))

; lambda as argument
(test-case "lambda as map arg" (equal? (map (lambda (x) (* x x)) (list 1 2 3)) (list 1 4 9)))

; subexpression calls — ((expr) args)
(test-case "call lambda expr" (= ((lambda (x) x) 42) 42))
(test-case "call lambda expr 2" (= ((lambda (x y) (+ x y)) 3 4) 7))
(test-case "call result of fn" (= ((make-adder 5) 10) 15))
(test-case "call lambda no args" (= ((lambda () 99)) 99))
(test-case "call lambda with lambda arg" (= ((lambda (f) (f 10)) (lambda (x) (* x x))) 100))

; closure via define-lambda (workaround for inner define)
(define (make-multiplier factor)
  (define mul (lambda (x) (* factor x)))
  mul)
(define triple (make-multiplier 3))
(test-case "define-lambda capture" (= (triple 7) 21))

; 2-level nested lambdas
(define (level1 a)
  (lambda (b) (+ a b)))
(define lev2 (level1 100))
(test-case "2-level closure" (= (lev2 5) 105))

; 2-level with 2 captured vars
(define (cap2 a b)
  (lambda (x) (+ a b x)))
(define h2 (cap2 10 20))
(test-case "2-level 2 captures" (= (h2 5) 35))

; 3-level nested lambdas
(define (outer3 a)
  (lambda (b) (lambda () a)))
(define outer3-f1 (outer3 100))
(define outer3-f2 (outer3-f1 200))
(test-case "3-level closure" (= (outer3-f2) 100))

; 4-level nesting across all levels
(define (lev-a x)
  (lambda (y)
    (lambda (z)
      (lambda () (+ x y z)))))
(define lev-b (lev-a 1))
(define lev-c (lev-b 2))
(define lev-d (lev-c 3))
(test-case "4-level closure" (= (lev-d) 6))

; set! across 3 levels
(define (make-deep-counter)
  (define n 0)
  (lambda ()
    (lambda ()
      (set! n (+ n 1))
      n)))
(define deep-mid (make-deep-counter))
(define deep-inc (deep-mid))
(test-case "deep set! 1" (= (deep-inc) 1))
(test-case "deep set! 2" (= (deep-inc) 2))

; inner (define (f args) body) captures enclosing vars
(define (outer-def x)
  (define (inner-def) x)
  (inner-def))
(test-case "inner define capture" (= (outer-def 42) 42))

; inner define with args captures enclosing vars
(define (outer-def2 x)
  (define (add-to-x y) (+ x y))
  (add-to-x 10))
(test-case "inner define with args" (= (outer-def2 5) 15))

; inner define returned as closure
(define (make-adder-def n)
  (define (adder x) (+ n x))
  adder)
(define add5 (make-adder-def 5))
(test-case "inner define as closure" (= (add5 10) 15))

; inner define 3-level nesting
(define (outer-3def a)
  (define (middle b)
    (define (inner-3) (+ a b))
    (inner-3))
  (middle 20))
(test-case "inner define 3-level" (= (outer-3def 10) 30))

;----------------------
; test numeric functions
;----------------------
(test-suite "numeric fns")

(test-case "abs positive" (= (abs 5) 5))
(test-case "abs negative" (= (abs -5) 5))
(test-case "abs zero" (= (abs 0) 0))
(test-case "quotient" (= (quotient 17 5) 3))
(test-case "quotient negative" (= (quotient -17 5) -3))
(test-case "remainder" (= (remainder 17 5) 2))
(test-case "remainder negative" (= (remainder -17 5) -2))
(test-case "expt" (= (expt 2 10) 1024))
(test-case "expt zero" (= (expt 5 0) 1))
(test-case "expt one" (= (expt 7 1) 7))
(test-case "sqrt exact" (= (sqrt 25) 5))
(test-case "sqrt 0" (= (sqrt 0) 0))
(test-case "floor" (= (floor 3.7) 3))
(test-case "floor negative" (= (floor -3.2) -4))
(test-case "ceiling" (= (ceiling 3.2) 4))
(test-case "ceiling negative" (= (ceiling -3.7) -3))
(test-case "truncate" (= (truncate 3.9) 3))
(test-case "truncate negative" (= (truncate -3.9) -3))
(test-case "round" (= (round 3.5) 4))
(test-case "round down" (= (round 3.4) 3))
(test-case "positive? true" (positive? 5))
(test-case "positive? false" (not (positive? -3)))
(test-case "positive? zero" (not (positive? 0)))
(test-case "negative? true" (negative? -3))
(test-case "negative? false" (not (negative? 5)))
(test-case "gcd" (= (gcd 12 8) 4))
(test-case "gcd coprime" (= (gcd 7 13) 1))
(test-case "gcd with zero" (= (gcd 5 0) 5))
(test-case "lcm" (= (lcm 4 6) 12))
(test-case "lcm with zero" (= (lcm 0 5) 0))

;-------------------------
; test predicate functions
;-------------------------
(test-case "procedure? native" (procedure? car))
(test-case "procedure? lambda" (procedure? (lambda (x) x)))
(test-case "procedure? non-proc" (not (procedure? 42)))

;----------------------
; test string functions
;----------------------
(test-suite "string fns")

(test-case "string-length" (= (string-length "hello") 5))
(test-case "string-length empty" (= (string-length "") 0))
(test-case "string-ref first" (= (char->integer (string-ref "hello" 0)) (char->integer #\h)))
(test-case "string-ref last" (= (char->integer (string-ref "hello" 4)) (char->integer #\o)))
(test-case "substring" (equal? (substring "hello world" 0 5) "hello"))
(test-case "substring middle" (equal? (substring "hello world" 6 11) "world"))
(test-case "substring empty" (equal? (substring "hello" 2 2) ""))
(test-case "string-copy" (equal? (string-copy "hello") "hello"))

;----------------------
; test list functions
;----------------------
(test-suite "list fns")

(test-case "list-ref first" (= (list-ref (list 10 20 30) 0) 10))
(test-case "list-ref last" (= (list-ref (list 10 20 30) 2) 30))
(test-case "list-tail" (equal? (list-tail (list 1 2 3 4 5) 2) (list 3 4 5)))
(test-case "list-tail zero" (equal? (list-tail (list 1 2 3) 0) (list 1 2 3)))
(test-case "member found" (equal? (member 3 (list 1 2 3 4 5)) (list 3 4 5)))
(test-case "member not found" (not (member 6 (list 1 2 3 4 5))))
(test-case "memq found" (pair? (memq 2 (list 1 2 3))))
(test-case "memq not found" (not (memq 6 (list 1 2 3))))
(test-case "assoc found" (equal? (assoc "b" (list (list "a" 1) (list "b" 2) (list "c" 3))) (list "b" 2)))
(test-case "assoc not found" (not (assoc "z" (list (list "a" 1) (list "b" 2)))))
(test-case "assq found" (pair? (assq 2 (list (list 1 "a") (list 2 "b") (list 3 "c")))))
(test-case "assq not found" (not (assq 9 (list (list 1 "a") (list 2 "b")))))

;----------------------------
; nil-valued variable lookup
;----------------------------
(define nil-global '())
(test-case "nil global lookup" (null? nil-global))

(define (test-nil-local-fn)
  (define y '())
  (null? y))
(test-case "nil local lookup" (test-nil-local-fn))


;----------------------
; module import system
;----------------------
(test-suite "math")

(import math)

; constants
(test-case "pi constant" (> pi 3.14))
(test-case "e constant" (> e 2.71))

; trig basics
(test-case "sin(0)" (= (sin 0) 0))
(test-case "cos(0)" (= (cos 0) 1))
(test-case "tan(0)" (= (tan 0) 0))

; inverse trig
(test-case "asin(0)" (= (asin 0) 0))
(test-case "acos(1)" (= (acos 1) 0))
(test-case "atan(0)" (= (atan 0) 0))

; log/exp
(test-case "exp(0)" (= (exp 0) 1))
(test-case "pow(2,10)" (= (pow 2 10) 1024))
(test-case "pow(3,0)" (= (pow 3 0) 1))

; random
(test-case "random range" (let ((r (random 100))) (and (>= r 0) (< r 100))))

; numeric predicates
(test-case "finite? number" (finite? 42))
(test-case "not nan? number" (not (nan? 42)))

; double import is safe
(import math)
(test-case "double import ok" (= (sin 0) 0))

;----------------------
; quote and symbol tests
;----------------------
(test-suite "quote")

; basic quote syntax
(test-case "quote symbol" (symbol? 'hello))
(test-case "quote keyword form" (symbol? (quote foo)))
(test-case "quote number" (= '42 42))
(test-case "quote string" (equal? '"hello" "hello"))
(test-case "quote bool true" (eq? '#t #t))
(test-case "quote bool false" (eq? '#f #f))
(test-case "quote nil" (null? '()))

; quoted lists
(test-case "quote list" (equal? '(1 2 3) (list 1 2 3)))
(test-case "quote nested" (pair? '(1 (2 3) 4)))
(test-case "quote nested car" (= (car '(1 2 3)) 1))
(test-case "quote nested cadr" (= (cadr '(1 2 3)) 2))
(test-case "quote symbols in list" (symbol? (car '(a b c))))

; symbol operations
(test-case "symbol? true" (symbol? 'x))
(test-case "symbol? false on string" (not (symbol? "x")))
(test-case "symbol? false on number" (not (symbol? 42)))
(test-case "symbol->string" (equal? (symbol->string 'hello) "hello"))
(test-case "string->symbol" (symbol? (string->symbol "world")))
(test-case "symbol round-trip" (equal? (symbol->string (string->symbol "test")) "test"))

;----------------------
; eval tests
;----------------------
(test-suite "eval")

(test-case "eval arithmetic" (= (eval "(+ 1 2)") 3))
(test-case "eval list" (equal? (eval "(list 1 2 3)") (list 1 2 3)))
(test-case "eval string" (equal? (eval "(string-append \"hello\" \" world\")") "hello world"))
(test-case "eval define" (begin (eval "(define eval-test-var 99)") (= eval-test-var 99)))
(test-case "eval nested" (= (eval "(+ (eval \"(+ 1 2)\") 4)") 7))
(define eval-sym-var 5)
(test-case "eval a bare symbol" (= (eval "eval-sym-var") 5))
(test-case "eval a bare number" (= (eval "42") 42))
(test-case "eval returns the last value" (= (eval "1 2 3") 3))
(test-case "eval loads a file, then reads on"
    (begin (eval "(load \"tests/eval_load.scm\") (define eval-after-load (lf-loaded-fn 2))")
           (= eval-after-load 14)))

;----------------------
;--- do loop tests ---
(test-suite "do loops")

; simple counter: sum 0..4
(test-case "do simple counter"
    (= 10 (do ((i 0 (+ i 1))
                (sum 0 (+ sum i)))
               ((= i 5) sum))))

; do with no step (variable stays constant)
(test-case "do no step"
    (= 42 (do ((i 0 (+ i 1))
                (x 42))
               ((= i 3) x))))

; do early exit (test true on first iteration)
(test-case "do early exit"
    (= 0 (do ((i 0 (+ i 1)))
              ((= i 0) i))))

; do with compound step expression (factorial)
(test-case "do compound step"
    (= 120 (do ((i 1 (+ i 1))
                 (fact 1 (* fact i)))
                ((> i 5) fact))))

;----------------------
; regression tests: symbol equality, quoted data, and call/return handling
;----------------------
(test-suite "regressions")

; symbols are compared by name
(test-case "eq? same symbol" (eq? 'a 'a))
(test-case "eq? different symbols" (not (eq? 'a 'b)))
(test-case "equal? lists of symbols" (equal? '(a b) '(a b)))

; language keywords and floats are ordinary data inside quote
(test-case "quoted keyword is a symbol" (equal? (car '(if a b)) (string->symbol "if")))
(test-case "quoted define form" (equal? (length '(define (f x) (if x 1 2))) 3))
(test-case "quoted float" (= (car (cdr '(a 4.5))) 4.5))

; a nested quote form keeps its order: (quote b), not (b quote)
(test-case "nested quote word" (equal? (car (cdr '(a (quote b)))) '(quote b)))
(test-case "nested quote apostrophe" (equal? (car (cdr '(a 'b))) '(quote b)))

; a call followed by more expressions is not a tail call
(define (reg-id x) x)
(define (reg-f y) (reg-id y) 7)
(test-case "call then constant" (= (reg-f 3) 7))
(test-case "call then constant in expression" (= (+ 1 (reg-f 3)) 8))

(define (reg-let-f y) (let ((e 1)) (reg-id y) e))
(test-case "call then variable in let" (= (reg-let-f 3) 1))

(define (reg-pair) (let ((e (cons 1 2))) (set-cdr! e 5) e))
(test-case "set-cdr! on let variable" (= (cdr (reg-pair)) 5))

(define (reg-no x) #f)
(define (reg-if x) (if (reg-no x) 1 2))
(test-case "call in if condition" (= (reg-if 0) 2))

(define (reg-and) (and (reg-id 9) 5))
(test-case "and: call in non-final position" (= (reg-and) 5))

; display pushes nothing, so repeated calls must not unbalance the stack
(define (reg-d x) (display "") 1)
(test-case "display in non-final position" (= (+ (reg-d 1) (+ (reg-d 2) (reg-d 3))) 3))

; and / or values and tail calls through them
(test-case "and returns last value" (= (and 1 2 3) 3))
(test-case "and stops at false" (not (and 1 #f 3)))
(test-case "or returns first true" (= (or #f #f 3) 3))
(test-case "or of all false" (not (or #f #f)))

(define (reg-and-loop i) (and #t (if (< i 100000) (reg-and-loop (+ i 1)) i)))
(test-case "tail call through and" (= (reg-and-loop 0) 100000))
(define (reg-or-loop i) (or #f (if (< i 100000) (reg-or-loop (+ i 1)) i)))
(test-case "tail call through or" (= (reg-or-loop 0) 100000))

; closures keep their captured variables across calls to other functions
(define (cl-id x) x)
(define (cl-mk1 a) (lambda () (+ (cl-id 5) a)))
(test-case "closure: upvalue read after a call" (= ((cl-mk1 10)) 15))
(define (cl-mk2 a) (lambda () (+ a (cl-id 5))))
(test-case "closure: upvalue read before a call" (= ((cl-mk2 10)) 15))
(define (cl-mk3 a) (lambda () (let ((b (cl-id 1))) (+ b a))))
(test-case "closure: let body calls then reads upvalue" (= ((cl-mk3 10)) 11))
(define (cl-mk4 a) (let ((inc (lambda (x) (+ x a)))) (lambda () (list (inc 1) (inc 2) a))))
(test-case "closure: calls a captured closure" (equal? ((cl-mk4 10)) '(11 12 10)))

; calling a captured function goes through the upvalue, not the local frame
(define (cl-mk5 f) (lambda () (f 5)))
(test-case "closure: call captured fn in tail position" (= ((cl-mk5 (lambda (x) (* x 2)))) 10))
(define (cl-mk6 f) (lambda () (+ 1 (f 5))))
(test-case "closure: call captured fn in non-tail position" (= ((cl-mk6 cl-id)) 6))
(define (cl-mk7 f) (lambda (y) (f y)))
(test-case "closure: call captured fn with an argument" (= ((cl-mk7 cl-id) 7) 7))
(define (cl-mk8 f) (lambda () (lambda () (f 5))))
(test-case "closure: call captured fn two levels deep" (= (((cl-mk8 cl-id))) 5))

(define (cl-counter) (let ((n 0)) (lambda () (set! n (+ n 1)) n)))
(define cl-c (cl-counter))
(cl-c)
(cl-c)
(test-case "closure: set! on captured variable" (= (cl-c) 3))

; closures share captured variables with their scope and with each other
(define (cl-pair) (let ((n 0)) (cons (lambda () (set! n (+ n 1)) n) (lambda () n))))
(define cl-p (cl-pair))
((car cl-p))
((car cl-p))
(test-case "closure: closures share a captured variable" (= ((cdr cl-p)) 2))
(define (cl-later) (let ((x 1)) (let ((g (lambda () x))) (set! x 2) (g))))
(test-case "closure: sees a set! made after it was created" (= (cl-later) 2))
(define (cl-acc n) (lambda (x) (set! n (+ n x)) n))
(define cl-a1 (cl-acc 10))
(define cl-a2 (cl-acc 100))
(cl-a1 1)
(test-case "closure: separate calls get separate variables" (equal? (list (cl-a1 1) (cl-a2 1)) '(12 101)))
(define (cl-collect i acc) (if (= i 0) acc (cl-collect (- i 1) (cons (lambda () i) acc))))
(test-case "closure: each tail-call iteration keeps its own variable"
    (equal? (map (lambda (f) (f)) (cl-collect 3 '())) '(1 2 3)))

;----------------------
; issue regressions: stack, tail calls, unspecified values, globals, let*, letrec
;----------------------
(test-suite "issue regressions")

; deep non-tail recursion fits the default stack
(define (rg-count n) (if (= n 0) 0 (+ 1 (rg-count (- n 1)))))
(test-case "deep non-tail recursion" (= (rg-count 10000) 10000))

; tail calls run in constant stack whatever the arities
(define rg-g #f)
(define (rg-f n acc) (if (= n 0) acc (rg-g (- n 1))))
(set! rg-g (lambda (n) (if (= n 0) 0 (rg-f (- n 1) n))))
(test-case "tail calls between different arities" (= (rg-f 200000 0) 1))
(define (rg-grow n) (if (= n 0) 'done (rg-wide n 1 2 3)))
(define (rg-wide a b c d) (rg-grow (- a (- d c))))
(test-case "tail call to a function with more arguments" (eq? (rg-grow 200000) 'done))
(define (rg-mk) (lambda (n) (if (= n 0) 0 ((rg-mk) (- n 1)))))
(test-case "tail call through a computed function" (= ((rg-mk) 200000) 0))

; natives without a result (display, newline) still yield one value
(test-case "display yields one value" (= (length (list (display "") (display ""))) 2))
(define (rg-disp-loop i) (begin (display "") (if (< i 100000) (rg-disp-loop (+ i 1)) i)))
(test-case "display in a begin inside a loop" (= (rg-disp-loop 0) 100000))

; globals are looked up by name when the code runs
(define (rg-fwd x) (rg-later x))
(define (rg-later x) (+ x 1))
(test-case "forward reference to a later define" (= (rg-fwd 1) 2))
(define (rg-redef) 1)
(define (rg-calls-redef) (rg-redef))
(define (rg-redef) 2)
(test-case "redefinition reaches earlier callers" (= (rg-calls-redef) 2))
(define rg-stub #f)
(define (rg-use-stub x) (rg-stub x))
(define (rg-stub x) (* x 10))
(test-case "define replaces an earlier variable define" (= (rg-use-stub 2) 20))
(define rg-x 5)
(define rg-x (+ rg-x 1))
(test-case "define can use the old value" (= rg-x 6))
(define (rg-ev n) (if (= n 0) #t (rg-od (- n 1))))
(define (rg-od n) (if (= n 0) #f (rg-ev (- n 1))))
(test-case "mutual recursion between top-level defines" (rg-ev 100000))

; let* binds in order; empty binding lists
(test-case "let* sees earlier bindings" (equal? (let* ((a 1) (b (+ a 1)) (c (* b 10))) (list a b c)) '(1 2 20)))
(define rg-shadow 100)
(test-case "let* binding shadows a global" (= (let* ((rg-shadow 1) (y rg-shadow)) y) 1))
(define (rg-star-loop i) (let* ((j (+ i 1))) (if (< j 100000) (rg-star-loop j) j)))
(test-case "let* body in tail position" (= (rg-star-loop 0) 100000))
(test-case "let with no bindings" (= (let () 5) 5))
(test-case "let* with no bindings" (= (let* () 5) 5))

; letrec
(test-case "letrec recursion" (= (letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5)) 120))
(test-case "letrec mutual recursion"
    (equal? (letrec ((ev? (lambda (n) (if (= n 0) #t (od? (- n 1)))))
                     (od? (lambda (n) (if (= n 0) #f (ev? (- n 1))))))
              (list (ev? 10) (od? 7) (ev? 100001)))
            '(#t #t #f)))
(define (rg-lr-loop x) (letrec ((loop (lambda (i acc) (if (= i 0) acc (loop (- i 1) (+ acc x)))))) (loop 100000 0)))
(test-case "letrec loop in constant stack" (= (rg-lr-loop 2) 200000))
(test-case "letrec with no bindings" (= (letrec () 5) 5))
(test-case "letrec with plain values" (= (letrec ((a 1) (b 2)) (+ a b)) 3))

;----------------------
; language features: symbols, dotted pairs, procedures as values, variadic
; procedures, apply, named let, call/cc
;----------------------
(test-suite "language features")

; symbols are interned: one object per name
(test-case "same symbol is eq?" (eq? 'abc (string->symbol "abc")))
(test-case "quoted keyword symbol is eq?" (eq? (car '(if)) (string->symbol "if")))
(test-case "symbol is not its string" (not (eq? 'abc "abc")))

; dotted pairs in quoted data
(test-case "dotted pair literal" (equal? '(1 . 2) (cons 1 2)))
(test-case "dotted list literal" (equal? (cdr (cdr '(a b . c))) 'c))
(test-case "dotted pair normalizes" (equal? '(1 . (2 3)) '(1 2 3)))
(test-case "association list literal" (= (cdr (car (cdr '((a . 1) (b . 2))))) 2))

; operators and list as values; identifiers with / and =
(test-case "map with +" (equal? (map + '(1 2) '(3 4)) '(4 6)))
(define lf-plus +)
(test-case "operator bound to a variable" (= (lf-plus 2 3) 5))
(test-case "map with list" (equal? (map list '(1 2) '(3 4)) '((1 3) (2 4))))
(define lf-a/b 6)
(test-case "identifier with /" (= lf-a/b 6))

; variadic procedures
(define (lf-all . xs) xs)
(define (lf-rest a . r) (list a r))
(test-case "rest parameter only" (equal? (lf-all 1 2 3) '(1 2 3)))
(test-case "rest parameter, no extras" (equal? (lf-all) '()))
(test-case "required and rest" (equal? (lf-rest 1 2 3) '(1 (2 3))))
(test-case "required, empty rest" (equal? (lf-rest 1) '(1 ())))
(test-case "lambda with a single rest symbol" (equal? ((lambda args args) 1 2) '(1 2)))
(test-case "lambda with dotted parameters" (equal? ((lambda (a b . c) (list a b c)) 1 2 3 4) '(1 2 (3 4))))
(define (lf-tl n . acc) (if (= n 0) acc (lf-tl (- n 1) n)))
(test-case "variadic tail calls" (equal? (lf-tl 100000) '(1)))

; apply
(test-case "apply with a list" (= (apply + '(1 2 3)) 6))
(test-case "apply with leading arguments" (= (apply + 1 2 '(3 4)) 10))
(test-case "apply a lambda" (= (apply (lambda (a b) (* a b)) '(3 4)) 12))
(test-case "apply a variadic procedure" (equal? (apply lf-all 1 '(2 3)) '(1 2 3)))
(test-case "apply list" (equal? (apply list '(1 2)) '(1 2)))
(define (lf-apply-loop n) (if (= n 0) 'done (apply lf-apply-loop (list (- n 1)))))
(test-case "apply in tail position" (eq? (lf-apply-loop 100000) 'done))
(test-case "apply passed to map" (equal? (map apply (list + *) '((1 2) (3 4))) '(3 12)))

; named let
(test-case "named let loop" (= (let loop ((i 0) (acc 1)) (if (= i 5) acc (loop (+ i 1) (* acc 2)))) 32))
(test-case "named let in constant stack" (= (let loop ((i 0)) (if (< i 100000) (loop (+ i 1)) i)) 100000))
(test-case "named let with no bindings" (= (let loop () 5) 5))
(define (lf-rev l) (let lp ((l l) (acc '())) (if (null? l) acc (lp (cdr l) (cons (car l) acc)))))
(test-case "named let shadowing a parameter" (equal? (lf-rev '(1 2 3)) '(3 2 1)))
(test-case "named let closures per iteration"
    (equal? (map (lambda (f) (f)) (let loop ((i 0) (acc '())) (if (= i 3) acc (loop (+ i 1) (cons (lambda () i) acc)))))
            '(2 1 0)))

; call/cc (escape-only)
(test-case "call/cc normal return" (= (call/cc (lambda (k) 5)) 5))
(test-case "call/cc escape" (= (call/cc (lambda (k) (+ 1 (k 42)))) 42))
(test-case "call/cc escape from map"
    (eq? (call/cc (lambda (k) (map (lambda (x) (if (= x 2) (k 'escaped) x)) '(1 2 3)))) 'escaped))
(test-case "call/cc escape to outer"
    (= (+ 1 (call/cc (lambda (outer) (+ 10 (call/cc (lambda (inner) (outer 100))))))) 101))
(test-case "call/cc escape to inner"
    (= (+ 1 (call/cc (lambda (outer) (+ 10 (call/cc (lambda (inner) (inner 100))))))) 111))
(define (lf-find p l) (call/cc (lambda (ret) (for-each (lambda (x) (if (p x) (ret x))) l) #f)))
(test-case "call/cc early exit from a search" (equal? (list (lf-find even? '(1 3 4 5)) (lf-find even? '(1 3))) '(4 #f)))
(define (lf-cc-loop i) (if (= i 10000) i (lf-cc-loop (+ 1 (call/cc (lambda (k) (k i)))))))
(test-case "call/cc in a loop" (= (lf-cc-loop 0) 10000))
(test-case "continuation is a procedure" (call/cc (lambda (k) (procedure? k))))

; arithmetic with one or zero arguments
(test-case "unary minus" (= (- 5) -5))
(test-case "unary minus float" (= (- 2.5) -2.5))
(define (lf-neg x) (- x))
(test-case "unary minus in a procedure" (= (lf-neg (lf-neg 7)) 7))
(test-case "unary divide" (and (= (/ 1) 1) (= (/ 2.0) 0.5)))
(test-case "zero-argument + and *" (and (= (+) 0) (= (*) 1)))
(test-case "one-argument + and *" (and (= (+ 3) 3) (= (* 4) 4)))
(test-case "apply unary minus" (= (apply - '(3)) -3))

(test-case "string-append of zero or one strings" (and (equal? (string-append) "") (equal? (string-append "a") "a")))

; numbers starting with a dot
(test-case "leading-dot float" (= .5 0.5))
(test-case "signed leading-dot floats" (and (= -.5 -0.5) (= +.25 0.25)))
(test-case "leading-dot float in a list" (equal? '(a .5) (list 'a 0.5)))
(test-case "dot still makes a pair" (equal? '(a . 5) (cons 'a 5)))

; internal defines are local to the body
(define lf-inner 'global)
(define (lf-hide) (define (lf-inner) 'local) (lf-inner))
(test-case "internal define is local" (eq? (lf-hide) 'local))
(test-case "internal define does not leak" (eq? lf-inner 'global))
(define (lf-def-a) (define (helper) 'a) (helper))
(define (lf-def-b) (define (helper) 'b) (helper))
(test-case "same inner name in two procedures" (equal? (list (lf-def-a) (lf-def-b)) '(a b)))
(define (lf-depth n) (define (me) n) (if (= n 0) (list (me)) (let ((r (lf-depth (- n 1)))) (append r (list (me))))))
(test-case "each call has its own inner define" (equal? (lf-depth 3) '(0 1 2 3)))
(define (lf-parity n)
  (define (ev? n) (if (= n 0) #t (od? (- n 1))))
  (define (od? n) (if (= n 0) #f (ev? (- n 1))))
  (list (ev? n) (od? n)))
(test-case "mutually recursive internal defines" (equal? (lf-parity 7) '(#f #t)))
(define (lf-seq x) (define a (+ x 1)) (define b (* a 2)) (define (c) (+ a b)) (c))
(test-case "internal defines see earlier ones" (= (lf-seq 1) 6))
(define (lf-def-loop n) (define m (- n 1)) (if (= n 0) 'done (lf-def-loop m)))
(test-case "tail call after internal defines" (eq? (lf-def-loop 100000) 'done))
(define (lf-def-count n) (define (step i acc) (if (= i n) acc (step (+ i 1) (+ acc 1)))) (step 0 0))
(test-case "inner loop in constant stack" (= (lf-def-count 100000) 100000))
(test-case "internal define in lambda" (= ((lambda (x) (define y (* x 2)) (+ x y)) 5) 15))
(test-case "internal define in let" (= (let ((x 3)) (define (sq) (* x x)) (sq)) 9))
(test-case "internal define in let*" (= (let* ((x 2) (y 3)) (define z (* x y)) z) 6))
(test-case "internal define in named let"
    (equal? (let loop ((i 0) (acc '())) (define next (+ i 1)) (if (= i 3) acc (loop next (cons i acc)))) '(2 1 0)))
(define (lf-def-rest . xs) (define (count l) (if (null? l) 0 (+ 1 (count (cdr l))))) (count xs))
(test-case "variadic internal define" (= (lf-def-rest 'a 'b 'c) 3))
(define (lf-def-closure n) (define (get) n) (define (inc!) (set! n (+ n 1))) (list get inc!))
(define lf-dc (lf-def-closure 10))
((cadr lf-dc))
(test-case "internal defines share captured state" (= ((car lf-dc)) 11))

; when / unless
(test-case "when true" (= (when (> 2 1) 'ignored 5) 5))
(test-case "when false is unspecified" (not (equal? (when #f 1) 1)))
(test-case "unless false" (eq? (unless #f 'yes) 'yes))
(test-case "unless true is unspecified" (not (equal? (unless #t 1) 1)))
(define (lf-when-loop n) (when (> n 0) (lf-when-loop (- n 1))))
(test-case "when body in tail position" (begin (lf-when-loop 100000) #t))
(test-case "when and unless as quoted symbols" (equal? '(when unless) (list 'when 'unless)))

; min, max, append, iota, sort
(test-case "n-ary min and max" (equal? (list (min 3 1 2) (max 3 1 2) (min 7) (max 7)) '(1 3 7 7)))
(test-case "append of any number of lists" (equal? (append '(1 2) '(3) '() '(4 5)) '(1 2 3 4 5)))
(test-case "append of none or one" (and (null? (append)) (equal? (append '(1)) '(1))))
(test-case "append shares the last list" (let ((tail '(3 4))) (eq? (cdr (cdr (append '(1 2) tail))) tail)))
(test-case "append onto a non-list" (equal? (append '(1) 2) (cons 1 2)))
; long-data sizes: big enough to exceed the VM stack (65536) normally; small under
; the GC-stress build, which collects at every allocation and traces all live data
(define lf-big (if (gc-stress?) 2000 100000))
(test-case "append a long list in constant stack" (= (length (append (iota lf-big) (quote (x)))) (+ lf-big 1)))
(test-case "iota" (equal? (list (iota 4) (iota 3 1) (iota 3 0 5) (iota 0)) '((0 1 2 3) (1 2 3) (0 5 10) ())))
(test-case "sort numbers" (equal? (sort '(3 1 4 1 5 9 2 6) <) '(1 1 2 3 4 5 6 9)))
(test-case "sort descending" (equal? (sort '(3 1 2) >) '(3 2 1)))
(test-case "sort empty and single" (and (null? (sort '() <)) (equal? (sort '(1) <) '(1))))
(test-case "sort strings" (equal? (sort (list "pear" "apple" "fig") string<?) '("apple" "fig" "pear")))
(test-case "sort is stable" (equal? (sort '((1 . a) (0 . b) (1 . c) (0 . d)) (lambda (x y) (< (car x) (car y))))
                                    '((0 . b) (0 . d) (1 . a) (1 . c))))
(test-case "sort a long list" (equal? (list-tail (sort (iota 10000) >) 9997) '(2 1 0)))
(test-case "length of the empty list" (= (length '()) 0))

; strings and characters
(test-case "string=?" (and (string=? "ab" "ab") (string=? "a" "a" "a") (not (string=? "a" "b"))))
(test-case "string<? and string>?" (and (string<? "a" "b" "c") (not (string<? "a" "c" "b")) (string>? "b" "a")))
(test-case "string<=? and string>=?" (and (string<=? "a" "a" "b") (string>=? "b" "b" "a") (not (string<=? "b" "a"))))
(test-case "char comparisons" (and (char=? #\a #\a) (char<? #\a #\b #\c) (char>? #\b #\a) (char<=? #\a #\a) (char>=? #\b #\a)))
(test-case "string->list" (equal? (string->list "abc") (list #\a #\b #\c)))
(test-case "string->list of empty" (null? (string->list "")))
(test-case "list->string" (equal? (list->string (list #\h #\i)) "hi"))
(test-case "list->string round trip" (equal? (list->string (string->list "hello")) "hello"))
(test-case "char predicates" (and (char-alphabetic? #\a) (not (char-alphabetic? #\1)) (char-numeric? #\5)
                                  (char-whitespace? #\space) (char-upper-case? #\A) (char-lower-case? #\a)
                                  (not (char-upper-case? #\a))))
(test-case "char case conversion" (and (char=? (char-upcase #\a) #\A) (char=? (char-downcase #\Q) #\q)
                                       (char=? (char-upcase #\1) #\1)))

; exact and inexact
(test-case "exact->inexact" (and (float? (exact->inexact 3)) (= (exact->inexact 3) 3.0) (float? (inexact 1))))
(test-case "inexact->exact" (and (= (inexact->exact 4.0) 4) (not (float? (inexact->exact 4.0))) (= (exact (round 2.6)) 3)))
(test-case "exact of an integer is itself" (= (exact 5) 5))
(test-case "a whole float prints with .0" (equal? (number->string 3.0) "3.0"))
(test-case "number->string of a float" (equal? (number->string 2.5) "2.5"))

; quasiquote
(define qq-x 5)
(define qq-l '(1 2 3))
(test-case "quasiquote without unquotes is quote" (equal? `(a (b c) "s" 1) '(a (b c) "s" 1)))
(test-case "unquote" (equal? `(a ,qq-x b) '(a 5 b)))
(test-case "unquote an expression" (equal? `(sum ,(+ 1 2)) '(sum 3)))
(test-case "unquote-splicing" (equal? `(a ,@qq-l b) '(a 1 2 3 b)))
(test-case "splicing at the start, alone and at the end"
    (and (equal? `(,@qq-l x) '(1 2 3 x)) (equal? `(,@qq-l) '(1 2 3)) (equal? `(x ,@qq-l) '(x 1 2 3))))
(test-case "splicing the empty list" (equal? `(a ,@'() b) '(a b)))
(test-case "several splices" (equal? `(,@qq-l ,@qq-l) '(1 2 3 1 2 3)))
(test-case "splices are copied, except the last piece"
    (let ((r `(,@qq-l 0)))
      (and (not (eq? r qq-l)) (equal? qq-l '(1 2 3)) (eq? (cdr `(0 ,@qq-l)) qq-l))))
(test-case "dotted quasiquote" (and (equal? `(a . ,qq-x) (cons 'a 5)) (equal? `(,@qq-l . t) '(1 2 3 . t))))
(test-case "quasiquote an atom" (and (eq? `sym 'sym) (= `,qq-x 5) (null? `())))
(test-case "nested lists" (equal? `((x ,qq-x) (y (,@qq-l))) '((x 5) (y (1 2 3)))))
(test-case "nested quasiquote keeps inner unquotes"
    (equal? `(a `(b ,(c ,qq-x))) '(a (quasiquote (b (unquote (c 5)))))))
(test-case "nested quasiquote ,,x" (equal? `(a `(b ,,qq-x)) '(a (quasiquote (b (unquote 5))))))
(test-case "long forms" (equal? (quasiquote (a (unquote qq-x) (unquote-splicing qq-l))) '(a 5 1 2 3)))
(test-case "quote inside quasiquote" (equal? `(a '(b ,qq-x)) '(a (quote (b 5)))))
(test-case "unquote as quoted data" (equal? '(a ,b ,@c `d) '(a (unquote b) (unquote-splicing c) (quasiquote d))))
(define (qq-build n) `(n ,n ,@(iota n)))
(test-case "quasiquote in a procedure" (equal? (qq-build 3) '(n 3 0 1 2)))
(define (qq-loop n acc) (if (= n 0) acc (qq-loop (- n 1) `(,n ,@acc))))
(test-case "quasiquote in a tail-recursive loop" (= (length (qq-loop 10000 '())) 10000))
(test-case "append is a procedure value" (equal? (apply append '((1) (2 3) (4))) '(1 2 3 4)))

; eval of an expression
(test-case "eval a quoted expression" (= (eval '(+ 1 2)) 3))
(test-case "eval a quasiquoted expression" (= (eval `(* ,qq-x 2)) 10))
(test-case "eval a symbol" (= (eval 'qq-x) 5))
(test-case "eval a define" (begin (eval '(define eval-datum-var 42)) (= eval-datum-var 42)))
(test-case "eval with an environment" (equal? (eval '(list 1 "two" #\3 4.5) (interaction-environment)) (list 1 "two" #\3 4.5)))
(test-case "eval quoted data" (equal? (eval ''(a b . c)) '(a b . c)))
(test-case "eval a lambda call" (= (eval '((lambda (a b) (- a b)) 10 4)) 6))
(test-case "eval builds code" (= (eval (list '+ 1 2 3)) 6))
(test-case "a string literal over two lines" (= (string-length "a
b") 3))

; cond clauses (R7RS)
(test-case "cond body of several expressions" (= (cond (#f 1) (else 2 3)) 3))
(test-case "cond =>" (eq? (cond ((assq 'b '((a . 1) (b . 2))) => (lambda (p) (if (= (cdr p) 2) 'two 'other))) (else 'no)) 'two))
(test-case "cond test-only clause" (= (cond (#f) ((+ 1 1)) (else 9)) 2))
(test-case "cond with no match keeps the stack" (let ((l (list 1 (cond (#f 2)) 3))) (and (= (length l) 3) (= (car (cdr (cdr l))) 3))))
(define (lf-cond-n n) (cond ((= n 0) 'zero) ((= n 1) 'one) ((= n 2) 'two) ((= n 3) 'three) ((= n 4) 'four)
                            ((= n 5) 'five) ((= n 6) 'six) ((= n 7) 'seven) ((= n 8) 'eight) ((= n 9) 'nine)
                            ((= n 10) 'ten) ((= n 11) 'eleven) ((= n 12) 'twelve) ((= n 13) 'thirteen)
                            ((= n 14) 'fourteen) ((= n 15) 'fifteen) ((= n 16) 'sixteen) ((= n 17) 'seventeen)
                            (else 'many)))
(test-case "cond with more than 16 clauses" (equal? (map lf-cond-n '(0 16 17 18)) '(zero sixteen seventeen many)))
(test-case "assv and memv" (and (equal? (assv 2 '((1 . a) (2 . b))) '(2 . b)) (equal? (memv 3 '(1 3 4)) '(3 4))))

; exceptions: raise, error, guard, with-exception-handler
(test-case "guard catches raise" (equal? (guard (e (#t (list 'caught e))) (raise 'boom)) '(caught boom)))
(test-case "guard picks the matching clause" (eq? (guard (e ((symbol? e) 'sym) ((string? e) 'str)) (raise "x")) 'str))
(test-case "guard clause with =>" (= (guard (e ((assq 'code e) => cdr)) (raise '((code . 42)))) 42))
(test-case "guard body value when nothing is raised" (= (guard (e (#t 0)) (+ 1 2)) 3))
(test-case "error object" (equal? (guard (e ((error-object? e) (list (error-object-message e) (error-object-irritants e))))
                                     (error "bad thing:" 1 'x))
                              '("bad thing:" (1 x))))
(test-case "guard catches a built-in error" (equal? (guard (e ((error-object? e) (error-object-message e))) (car 5))
                                                   "argument to 'car' is not a pair"))
(test-case "guard catches an unbound variable" (eq? (guard (e ((error-object? e) 'unbound)) (lf-no-such-variable)) 'unbound))
(test-case "guard catches an arithmetic error" (eq? (guard (e (#t 'caught)) (+ 1 "x")) 'caught))
(test-case "guard catches a wrong argument count" (eq? (guard (e (#t 'caught)) ((lambda (a) a))) 'caught))
(test-case "guard catches division by zero" (eq? (guard (e (#t 'div)) (/ 1 0)) 'div))
(test-case "unmatched guard re-raises to the outer one"
    (eq? (guard (e ((string? e) 'outer)) (guard (e2 ((number? e2) 'inner)) (raise "s"))) 'outer))
(test-case "a raise from inside map" (eq? (guard (e ((symbol? e) e)) (map (lambda (x) (if (= x 2) (raise 'two) x)) '(1 2 3))) 'two))
(define (lf-guard-loop n) (if (= n 0) 'done (begin (guard (e (#t #f)) (raise 'x)) (lf-guard-loop (- n 1)))))
(test-case "guard in a loop" (eq? (lf-guard-loop 10000) 'done))
(test-case "raise-continuable" (= (with-exception-handler (lambda (c) 10) (lambda () (+ 1 (raise-continuable 'c)))) 11))
(test-case "a handler returning from raise is an error"
    (guard (e ((error-object? e) (equal? (error-object-irritants e) '(x))))
      (with-exception-handler (lambda (c) 'ignored) (lambda () (raise 'x)))))
(test-case "a handler can escape with call/cc"
    (equal? (call/cc (lambda (k) (with-exception-handler (lambda (c) (k (list 'escaped c))) (lambda () (raise 'oops)))))
            '(escaped oops)))
(test-case "a handler runs with the outer handlers"
    (eq? (guard (e ((eq? e 'second) 'outer-saw-second))
           (with-exception-handler (lambda (c) (raise 'second)) (lambda () (raise 'first))))
         'outer-saw-second))
(test-case "handlers are uninstalled after the body" (begin (guard (e (#t #f)) 1) (guard (e (#t 'fresh)) (raise 'y))))
(test-case "error objects print readably" (error-object? (guard (e (#t e)) (error "m" 1))))

; read abbreviations
(define lf-read-port (open-input-file "tests/read_abbrev.txt"))
(test-case "read 'x" (equal? (read lf-read-port) ''x))
(test-case "read quasiquote and unquotes" (equal? (read lf-read-port) '(quasiquote (a (unquote b) (unquote-splicing c)))))
(test-case "read (quote y)" (equal? (read lf-read-port) ''y))
(test-case "read nested quotes" (equal? (read lf-read-port) '(quote (1 (quote 2)))))

; vectors
(define lf-v (vector 1 2 3))
(test-case "vector and vector-ref" (and (= (vector-ref lf-v 0) 1) (= (vector-ref lf-v 2) 3)))
(test-case "vector-length" (and (= (vector-length lf-v) 3) (= (vector-length #()) 0)))
(test-case "vector-set!" (let ((v (vector 1 2 3))) (vector-set! v 1 'x) (equal? v #(1 x 3))))
(test-case "vector literal" (equal? #(a "s" #\c 1.5 (1 2) #(n)) (vector 'a "s" #\c 1.5 '(1 2) (vector 'n))))
(test-case "quoted vector literal" (equal? '#(1 2) (vector 1 2)))
(test-case "vector literal elements are data" (eq? (vector-ref #(x) 0) 'x))
(test-case "vector?" (and (vector? #(1)) (vector? (vector)) (not (vector? '(1))) (not (vector? "s"))))
(test-case "make-vector" (and (equal? (make-vector 3 'z) #(z z z)) (= (vector-length (make-vector 5)) 5)))
(test-case "vector->list" (and (equal? (vector->list #(1 2 3)) '(1 2 3)) (equal? (vector->list #(1 2 3 4) 1 3) '(2 3))
                               (null? (vector->list #()))))
(test-case "list->vector" (equal? (list->vector '(a b c)) #(a b c)))
(test-case "vector-fill!" (let ((v (make-vector 4 0))) (vector-fill! v 7 1 3) (equal? v #(0 7 7 0))))
(test-case "vector-copy" (and (equal? (vector-copy #(1 2 3 4) 2) #(3 4)) (equal? (vector-copy #(1 2 3) 0 2) #(1 2))))
(test-case "vector-copy is a copy" (let* ((v (vector 1 2)) (c (vector-copy v))) (vector-set! c 0 9) (= (vector-ref v 0) 1)))
(test-case "vector-append" (equal? (vector-append #(1) #() #(2 3)) #(1 2 3)))
(test-case "vector-map" (and (equal? (vector-map (lambda (x) (* x x)) #(1 2 3)) #(1 4 9))
                             (equal? (vector-map + #(1 2 3) #(10 20 30 40)) #(11 22 33))))
(test-case "vector-for-each" (let ((sum 0)) (vector-for-each (lambda (x) (set! sum (+ sum x))) #(1 2 3)) (= sum 6)))
(test-case "equal? on vectors" (and (equal? #(1 (2 #(3))) (vector 1 (list 2 (vector 3)))) (not (equal? #(1 2) #(1 2 3)))
                                    (not (equal? #(1) '(1)))))
(test-case "equal? on long lists" (equal? (iota lf-big) (iota lf-big)))
(test-case "quasiquoted vector" (let ((n 5)) (equal? `#(1 ,n ,@(list 2 3)) #(1 5 2 3))))
(test-case "eval of a vector" (and (= (eval '(vector-ref #(10 20) 1)) 20) (equal? (eval (list 'quote (vector 1 2))) #(1 2))))
(test-case "large vectors" (and (= (vector-length (list->vector (iota lf-big))) lf-big)
                                (= (length (vector->list (make-vector lf-big 1))) lf-big)))
(test-case "vector index errors are catchable" (eq? (guard (e ((error-object? e) 'bad-index)) (vector-ref #(1 2) 5)) 'bad-index))
(test-case "a vector of vectors" (let ((m (vector (vector 1 2) (vector 3 4)))) (= (vector-ref (vector-ref m 1) 0) 3)))

; case (R7RS)
(define (lf-case-sym x) (case x ((red green) 'color) ((circle square) 'shape) (else 'other)))
(test-case "case with symbol datums" (equal? (map lf-case-sym '(red square tree)) '(color shape other)))
(test-case "case with keyword datums" (eq? (case 'if ((if then) 'kw) (else 'no)) 'kw))
(test-case "case with characters" (eq? (case #\b ((#\a #\b) 'ab) (else 'no)) 'ab))
(test-case "case body of several expressions" (= (case 2 ((2) (display "") 20) (else 0)) 20))
(test-case "case =>" (= (case 5 ((5) => (lambda (k) (* k 10))) (else 0)) 50))
(test-case "case else =>" (equal? (case 6 ((5) 'no) (else => list)) '(6)))
(test-case "case with no match keeps the stack" (let ((l (list 1 (case 9 ((1) 'a)) 3))) (and (= (length l) 3) (= (car (cdr (cdr l))) 3))))
(test-case "case with an empty datum list" (eq? (case 1 (() 'never) (else 'e)) 'e))
(define (lf-case-loop n) (case n ((0) 'done) (else (lf-case-loop (- n 1)))))
(test-case "case else body in tail position" (eq? (lf-case-loop 100000) 'done))
(test-case "case is not fooled by redefining eqv?" (let () (define (eqv? a b) #t) (eq? (case 1 ((2) 'two) (else 'other)) 'other)))

; hash tables
(define lf-h (make-hash-table))
(hash-table-set! lf-h 'a 1)
(hash-table-set! lf-h "str" 2)
(hash-table-set! lf-h '(1 2) 3)
(hash-table-set! lf-h '() 'empty)
(hash-table-set! lf-h #(1 2) 'vec)
(hash-table-set! lf-h 2.5 'flt)
(hash-table-set! lf-h #\c 'chr)
(test-case "hash-table-ref with equal? keys"
    (equal? (list (hash-table-ref lf-h 'a) (hash-table-ref lf-h (string-append "s" "tr")) (hash-table-ref lf-h (list 1 2))
                  (hash-table-ref lf-h '()) (hash-table-ref lf-h (vector 1 2)) (hash-table-ref lf-h 2.5) (hash-table-ref lf-h #\c))
            '(1 2 3 empty vec flt chr)))
(test-case "hash-table-count" (= (hash-table-count lf-h) 7))
(test-case "hash-table-set! replaces" (begin (hash-table-set! lf-h 'a 10) (and (= (hash-table-ref lf-h 'a) 10) (= (hash-table-count lf-h) 7))))
(test-case "hash-table-ref/default" (eq? (hash-table-ref/default lf-h 'missing 'dflt) 'dflt))
(test-case "hash-table-ref failure and success" (and (eq? (hash-table-ref lf-h 'missing (lambda () 'thunk)) 'thunk)
                                                    (= (hash-table-ref lf-h 'a (lambda () 0) (lambda (v) (* v 2))) 20)))
(test-case "a missing key is a catchable error" (error-object? (guard (e (#t e)) (hash-table-ref lf-h 'missing))))
(test-case "hash-table-contains? and delete!" (and (hash-table-contains? lf-h 'a) (begin (hash-table-delete! lf-h 'a) (not (hash-table-contains? lf-h 'a)))))
(test-case "the empty list as a value" (begin (hash-table-set! lf-h 'n '()) (null? (hash-table-ref lf-h 'n (lambda () 'missing)))))
(define lf-counts (make-hash-table))
(for-each (lambda (w) (hash-table-update!/default lf-counts w (lambda (n) (+ n 1)) 0)) '(a b a c a b))
(test-case "hash-table-update!/default counting" (and (= (hash-table-ref lf-counts 'a) 3) (= (hash-table-ref lf-counts 'b) 2) (= (hash-table-ref lf-counts 'c) 1)))
(test-case "hash-table-update!" (begin (hash-table-update! lf-counts 'a (lambda (n) (* n 10))) (= (hash-table-ref lf-counts 'a) 30)))
(test-case "hash-table-update! with a failure thunk" (begin (hash-table-update! lf-counts 'z (lambda (n) (+ n 1)) (lambda () 100)) (= (hash-table-ref lf-counts 'z) 101)))
(test-case "hash-table-keys, values and ->alist"
    (and (= (length (hash-table-keys lf-counts)) 4) (= (apply + (hash-table-values lf-counts)) 134)
         (equal? (assq 'b (hash-table->alist lf-counts)) '(b . 2))))
(test-case "eq? tables compare by identity"
    (let ((e (make-hash-table eq?)) (k (list 1)))
      (hash-table-set! e k 'y)
      (and (eq? (hash-table-ref e k) 'y) (eq? (hash-table-ref/default e (list 1) 'not-found) 'not-found))))
(test-case "string=? tables" (let ((s (make-hash-table string=?))) (hash-table-set! s "k" 1) (= (hash-table-ref s (string-append "" "k")) 1)))
(test-case "hash-table-copy and clear!"
    (let ((c (hash-table-copy lf-counts))) (hash-table-clear! lf-counts) (and (= (hash-table-count lf-counts) 0) (= (hash-table-count c) 4))))
(test-case "alist->hash-table keeps the first entry" (= (hash-table-ref (alist->hash-table '((x . 1) (y . 2) (x . 3))) 'x) 1))
(test-case "hash-table-walk can change the table"
    (let ((w (alist->hash-table '((x . 1) (y . 2)))))
      (hash-table-walk w (lambda (k v) (hash-table-set! w (list k) v)))
      (= (hash-table-count w) 4)))
(test-case "hash-table?" (and (hash-table? lf-h) (not (hash-table? '()))))
(define lf-big-h (make-hash-table))
(let loop ((i 0)) (when (< i 20000) (hash-table-set! lf-big-h i (+ i 1)) (loop (+ i 1))))
(test-case "a large hash table" (and (= (hash-table-count lf-big-h) 20000) (= (hash-table-ref lf-big-h 19999) 20000)))
(let loop ((i 0)) (when (< i 20000) (hash-table-delete! lf-big-h i) (loop (+ i 1))))
(test-case "deleting everything" (= (hash-table-count lf-big-h) 0))

; report test results
;----------------------
(test-end)

