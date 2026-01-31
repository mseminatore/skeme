;-----------------------------------------------
; Skeme error handling test cases
;
; Tests that error handling works properly without assertions
; Copyright (C) 2026 by Mark Seminatore
;-----------------------------------------------

(load "stdlib.scm")
(load "skunit.scm")

;-----------------------
; Test error conditions
;-----------------------

; Test that we can define many symbols without crashing (symbol table growth)
(define sym1 1)
(define sym2 2)
(define sym3 3)
(define sym4 4)
(define sym5 5)
(test-case "define many symbols" (and (equal? sym1 1) (equal? sym5 5)))

; Test that functions can be defined without assertion failures
(define (test-fn x) (+ x 1))
(test-case "define function without crash" (equal? (test-fn 5) 6))

; Test that lambdas work without assertion failures  
(define my-lambda (lambda (x) (* x 2)))
(test-case "lambda without crash" (equal? (my-lambda 5) 10))

(test-end)
