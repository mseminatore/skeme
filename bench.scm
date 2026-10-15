; bench.scm - Benchmark suite for profiling
(load "stdlib.scm")

;--- Recursive fibonacci (CPU-bound, dispatch-heavy) ---
(define (fib n)
    (if (<= n 2)
        1
        (+ (fib (- n 1)) (fib (- n 2)))))

(display "fib(25) = ")
(display (fib 25))
(newline)

;--- List building and traversal ---
(define (make-list-helper i n acc)
    (if (= i 0)
        acc
        (make-list-helper (- i 1) n (cons i acc))))

(define (make-list n)
    (make-list-helper n n '()))

(define (list-sum lst)
    (if (null? lst)
        0
        (+ (car lst) (list-sum (cdr lst)))))

(display "list-sum(1..100) = ")
(display (list-sum (make-list 100)))
(newline)

(display "Benchmark complete.\n")
