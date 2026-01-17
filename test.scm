;(load "stdlib.scm")
;(load "core.scm")
;(load "io.scm")
;(load "../fib.scm")

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
      condition))

;----------------------
; define test case runner
;----------------------
(define (test-case name test-fn)
  (display (string-append "Running test case: " name "..."))
  (if test-fn (display "OK.") test-fn)
  (newline))

;----------------------
; define a variable
;----------------------
(define five 5)

;----------------------
; construct a pair
;----------------------
(assert (cons 1 2) "pair cons failed")
(define p (cons 1 2))
(assert (pair? (cons 1 2)) "cons not a pair")
(assert (pair? p) "p not a pair")

;----------------------
; make list via nested cons
;----------------------
(assert (cons 1 (cons 2 ())) "nested cons failed")
(assert (list? (cons 1 (cons 2 ()))) "list? failed")

;----------------------
; make a list
;----------------------
(assert (list 1 2 3) "list failed")
(define l (list 1 2 3))
(assert (list? l) "list? failed")
(assert (= (car l) 1) "car failed")
(assert (list? (cdr l)) "cdr failed")

;----------------------
; characterization operations
;----------------------
(assert (zero? 0) "zero? failed")
(assert (not (zero? 1)) "zero? failed")
(assert (null? ()) "empty list check failed")
(assert (number? 1) "number? failed")
(assert (not (number? #t)) "number? failed")
(assert (string? "foo") "string? failed")
(assert (atom? 1) "atom? failed")
(assert (atom? five) "atom? failed")
(assert (not (atom? l)) "atom? failed")
(assert (not (atom? p)) "atom? failed")
(assert (= (length "foo") 3) "length failed")
(assert (eq? "foo" "foo") "eq? strings failed")
(assert (not (eq? "foo" "bar")) "eq? strings failed")

(assert (integer? 1) "integer? failed")
(assert (not (integer? #t)) "integer? failed")
;(assert (not (integer? 1.5)) "integer? failed")

;----------------------
; logical operations
;----------------------
(assert (boolean? #t) "boolean? failed")
(assert (boolean? #f) "boolean? failed")
(assert (not (boolean? 1)) "boolean? failed")

(assert (and 1 2 3) "and failed")
(assert (or 1 1 1) "or failed")
(assert (not #f) "not failed")

;----------------------
; comparison operations
;----------------------
(assert (if (< 1 2) #t) "if failed")
(assert (cond ((< 1 2) #t)) "cond failed")
(assert (cond ((> 1 2) #f) (else #t)) "cond else failed")

;(define (cadr x) (car (cdr x)))
;(define (cddr x) (cdr (cdr x)))

;----------------------
; define a function
;----------------------
(define (add a b)
    (+ a b))

(assert (= (add 2 3) 5) "add(2,3) failed")
(assert (= (add -1 1) 0) "add(-1,1) failed")
(assert (= (add 0 0) 0) "add(0,0) failed")
(assert (= (add 100 200) 300) "add(100, 200) failed")

;----------------------
; recursive function
;----------------------
(define (factorial n)
    (if (zero? n)
        1
        (* n (factorial (- n 1)))))
        
(assert (= (factorial 5) 120) "factorial function failed")
(assert (= (factorial 0) 1) "factorial function failed")
(assert (= (factorial 1) 1) "factorial function failed")
(assert (= (factorial 6) 720) "factorial function failed")
(assert (= (factorial 10) 3628800) "factorial function failed")

;----------------------
; test fibonacci function
;----------------------
(define (fibonacci n)
    (if (<= n 1)
        n
        (+ (fibonacci (- n 1)) (fibonacci (- n 2)))))

(assert (= (fibonacci 10) 55) "fibonacci(10) failed")
(assert (= (fibonacci 0) 0) "fibonacci(0) failed")
(assert (= (fibonacci 1) 1) "fibonacci(1) failed")
(assert (= (fibonacci 5) 5) "fibonacci(5) failed")
(assert (= (fibonacci 15) 610) "fibonacci(15) failed")
(assert (= (fibonacci 20) 6765) "fibonacci(20) failed")
(assert (= (fibonacci 25) 75025) "fibonacci(25) )failed")
(assert (= (fibonacci 30) 832040) "fibonacci(30) failed")

;----------------------
; test tail-recursive fibonacci
;----------------------
(define (fib-iter a b count)
  (if (= count 0)
    b
    (fib-iter (+ a b) a (- count 1))))

(define (fib n)
  (fib-iter 1 0 n))

(assert (= (fib 10) 55) "tail-recursive fib(10) failed")
(assert (= (fib 0) 0) "tail-recursive fib(0) failed")
(assert (= (fib 1) 1) "tail-recursive fib(1) failed")
(assert (= (fib 5) 5) "tail-recursive fib(5) failed")
(assert (= (fib 15) 610) "tail-recursive fib(15) failed")
(assert (= (fib 20) 6765) "tail-recursive fib(20) failed")

;----------------------
; passed param test
;----------------------
(define (bind1 f x) (f x))
(define (sqr x) (* x x))
(assert (= (bind1 sqr 5) 25) "passed param test failed")

;(assert (= (bind (lambda (y) (+ y 10)) 5) 15) "passed param test failed")


;----------------------
; if reached here, all tests passed
;----------------------
(gc-stats)
(display "All tests passed successfully.")
(newline)
