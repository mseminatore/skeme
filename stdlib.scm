;-----------------------------------------------
; Skeme standard library functions
;
; Copyright (C) 2026 by Mark Seminatore
;-----------------------------------------------

(load "term_colors.scm")

;----------------------
; define assertion test
;----------------------
(define (assert condition message)
  (if (not condition)
      (error "Assertion failed:" message)
      condition)
)

;----------------------
; define cadr and cddr
;----------------------
(define (cadr x) (car (cdr x)))
(define (cddr x) (cdr (cdr x)))

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

;----------------------
; define even?
;----------------------
(define (even? n)
  (= (modulo n 2) 0)
)

;----------------------
; define odd?
;----------------------
(define (odd? n)
  (not (even? n))
)

;----------------------
; define min/max
;----------------------
(define (min a b)
    (if (< a b) a b)
)

(define (max a b)
    (if (> a b) a b)
)

;----------------------
; define append
;----------------------
(define (append lst1 lst2)
  (if (null? lst1)
      lst2
      (cons (car lst1) (append (cdr lst1) lst2))
  )
)

;----------------------
; define map
; map function over a list
;----------------------
;(define (map fn lst)
;  (if (null? lst)
;      '()
;      (cons (fn (car lst)) (map fn (cdr lst)))
;  )
;)

;----------------------
; list-ref: return nth element of a list
;----------------------
(define (list-ref lst n)
  (if (= n 0)
      (car lst)
      (list-ref (cdr lst) (- n 1))))

;----------------------
; list-tail: return the sublist after dropping n elements
;----------------------
(define (list-tail lst n)
  (if (= n 0)
      lst
      (list-tail (cdr lst) (- n 1))))

;----------------------
; member: find first element equal? to x
;----------------------
(define (member x lst)
  (cond
    ((null? lst) #f)
    ((equal? x (car lst)) lst)
    (else (member x (cdr lst)))))

;----------------------
; memq: find first element eq? to x
;----------------------
(define (memq x lst)
  (cond
    ((null? lst) #f)
    ((eq? x (car lst)) lst)
    (else (memq x (cdr lst)))))

;----------------------
; assoc: find pair by key using equal?
;----------------------
(define (assoc key alist)
  (cond
    ((null? alist) #f)
    ((equal? key (car (car alist))) (car alist))
    (else (assoc key (cdr alist)))))

;----------------------
; assq: find pair by key using eq?
;----------------------
(define (assq key alist)
  (cond
    ((null? alist) #f)
    ((eq? key (car (car alist))) (car alist))
    (else (assq key (cdr alist)))))

;----------------------
; for-each: apply fn to each element for side effects
;----------------------
(define (for-each fn lst)
  (if (null? lst)
      '()
      (begin
        (fn (car lst))
        (for-each fn (cdr lst)))))

;----------------------
; gcd: greatest common divisor
;----------------------
(define (gcd a b)
  (if (= b 0)
      (abs a)
      (gcd b (remainder a b))))

;----------------------
; lcm: least common multiple
;----------------------
(define (lcm a b)
  (if (= a 0)
      0
      (abs (quotient (* a b) (gcd a b)))))
