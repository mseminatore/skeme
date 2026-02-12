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
(test-case "apply plus1" (equal? (list 2 3 4) (apply plus1 (list 1 2 3))))

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

; Note: passing lambdas directly as arguments requires closure support
; (test-case "pass lambda to fn" (= (bind1 (lambda (n) (+ n 1)) 10) 11))

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
; report test results
;----------------------
(test-end)

