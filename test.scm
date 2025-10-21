; a number
5

; an empty list
()

; construct a pair
(cons 1 2)
(pair? (cons 1 2))

; make a nested list
(cons 1 (cons 2 ()))

(list? (cons 1 (cons 2 ())))

(zero? 0)
(zero? 1)
(null? ()))
(number? 1)
(boolean? #t)

; define a variable
;(define foo 5)

; a function
;(define (fib n)
;    (if (<= n 2)
;        1
;        (+ (fib (- n 1)) (fib (- n 2)))))
