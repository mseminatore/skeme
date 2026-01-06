;(load "stdlib.scm")
;(load "core.scm")
;(load "io.scm")
;(load "../fib.scm")

;----------------------
; define assertion test
;----------------------
(define (assert condition message)
  (if (not condition)
      (error "Assertion failed:" message)
      condition))

; turn on VM logging
;(vmlog)

; turn on parser logging
;(parselog)

; turn on GC logging
;(gclog)

; construct a pair
(assert (cons 1 2) "pair cons failed")
(define p (cons 1 2))
(assert (pair? (cons 1 2)) "cons not a pair")
(assert (pair? p) "p not a pair")

; make a list via nested cons
(cons 1 (cons 2 ()))
(list? (cons 1 (cons 2 ())))

; make a list
(list 1 2 3)
(define l (list 1 2 3))
(list? l)
(car l)
(cdr l)

(zero? 0)
(zero? 1)
(null? ())
(number? 1)
(boolean? #t)
(and 1 2 3)
(or 1 1 1)
(if (< 1 2) #t)
(cond ((< 1 2) #t))
(cond ((> 1 2) #f) (else #t))

; define a variable
;(define foo 5)

;(define (cadr x) (car (cdr x)))
;(define (cddr x) (cdr (cdr x)))

; define a function
(define (add a b)
    (+ a b))

(assert (= (add 2 3) 5) "add(2,3) failed")
(assert (= (add -1 1) 0) "add(-1,1) failed")
(assert (= (add 0 0) 0) "add(0,0) failed")
(assert (= (add 100 200) 300) "add(100, 200) failed")

; recursive function
(define (factorial n)
    (if (zero? n)
        1
        (* n (factorial (- n 1)))))
        
(assert (= (factorial 5) 120) "factorial function failed")
(assert (= (factorial 0) 1) "factorial function failed")
(assert (= (factorial 1) 1) "factorial function failed")
(assert (= (factorial 6) 720) "factorial function failed")
(assert (= (factorial 10) 3628800) "factorial function failed")

;
; test fibonacci function
;
(define (fibonacci n)
    (if (<= n 2)
        1
        (+ (fibonacci (- n 1)) (fibonacci (- n 2)))))

(assert (= (fibonacci 10) 55) "fibonacci(10) failed")
;(assert (= (fibonacci 0) 0) "fibonacci(0) failed")
(assert (= (fibonacci 1) 1) "fibonacci(1) failed")
(assert (= (fibonacci 5) 5) "fibonacci(5) failed")
(assert (= (fibonacci 15) 610) "fibonacci(15) failed")
(assert (= (fibonacci 20) 6765) "fibonacci(20) failed")
(assert (= (fibonacci 25) 75025) "fibonacci(25) )failed")
(assert (= (fibonacci 30) 832040) "fibonacci(30) failed")

; if reached here, all tests passed
(display "All tests passed successfully.")
(newline)
; end of test.scm
