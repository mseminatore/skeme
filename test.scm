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

;(define (assert condition message) (if (not condition) (error "Assertion failed:" message) condition))