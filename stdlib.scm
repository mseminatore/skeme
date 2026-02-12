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
; define apply
;----------------------
(define (apply fn lst)
  (cond
    ((null? lst) '())
    (else (cons (fn (car lst)) (apply fn (cdr lst))))
  )
)

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
