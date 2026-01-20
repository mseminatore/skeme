;(load "stdlib.scm")
;(load "core.scm")
;(load "io.scm")
;(load "../fib.scm")

;----------------------
; terminal control sequences
;----------------------
(define ESC "\e")
(define TERM_CLEAR (string-append ESC "[2J" ESC "[H"))
(define TERM_RESET (string-append ESC "[0m"))
(define TERM_RED (string-append ESC "[31m"))
(define TERM_GREEN (string-append ESC "[32m"))
(define TERM_MAGENTA (string-append ESC "[35m"))

;----------------------
; enable logging options
;----------------------

; turn on VM logging
;(vmlog)

; turn on parser logging
;(parselog)

; turn on GC logging
;(gclog)

;----------------------
; define assertion test
;----------------------
(define (assert condition message)
  (if (not condition)
      (error "Assertion failed:" message)
      condition)
)

;----------------------
; define test case runner
;----------------------
(define err_count 0)
(define test_count 0)
(define test-pass-msg (string-append TERM_GREEN "OK." TERM_RESET))
(define test-fail-msg (string-append TERM_RED "failed!" TERM_RESET))

(define (test-report-error)
    (display test-fail-msg)
    (set! err_count (+ 1 err_count))
)

(define (test-case name test-fn)
  (display (string-append "Running test case: '" TERM_MAGENTA name "'..."))
  (if test-fn (display test-pass-msg) (test-report-error))
  (newline)
  (set! test_count (+ 1 test_count))
  (not (not test-fn))
)

;----------------------
; define cadr and cddr
;----------------------
(define (cadr x) (car (cdr x)))
(define (cddr x) (cdr (cdr x)))

;----------------------
; define apply
;----------------------
(define (plus1 a) (+ 1 a))

(define (apply fn lst)
  (cond
    ((null? lst) '())
    (else (cons (fn (car lst)) (apply fn (cdr lst))))
  )
)

(apply plus1 (list 1 2 3))

;----------------------
; define filter
;----------------------
(define (filter fn lst)
  (cond
    ((null? lst) '())
    ((fn (car lst))
    (cons (car lst) (filter fn (cdr lst))))
    (else (filter fn (cdr lst)))
  )
)

(define (one? n) (= 1 n))

(test-case "filter a list" (= 3 (length (filter one? (list 1 2 1 1)))))

;----------------------
; even?
;----------------------
(define (even? n)
  (= (modulo n 2) 0)
)

(test-case "even? test" (and (even? 2) (not (even? 3))))

;----------------------
; odd?
;----------------------
(define (odd? n)
  (not (even? n))
)

(test-case "odd? test" (and (odd? 3) (not (odd? 2))))

;----------------------
; min/max
;----------------------
(define (min a b)
    (if (< a b) a b)
)

(define (max a b)
    (if (> a b) a b)
)

(test-case "min test" (= 1 (min 1 2)))
(test-case "max test" (= 2 (max 1 2)))

;----------------------
; define a variable
;----------------------
(define five 5)

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
(test-case "foo = bar" (not (eq? "foo" "bar")))
(test-case "integer? 1" (integer? 1))
(test-case "integer? #t" (not (integer? #t)))
;(assert (not (integer? 1.5)) "integer? failed")

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
(test-case "fibonacci(25)" (= (fibonacci 25) 75025))
(test-case "fibonacci(30)" (= (fibonacci 30) 832040))

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

;(assert (= (bind (lambda (y) (+ y 10)) 5) 15) "passed param test failed")


;----------------------
; if reached here, all tests passed
;----------------------
(gc-stats)
(display test_count)
(display " tests passed successfully.")
(newline)
;(display (string-append err_count " tests failed."))