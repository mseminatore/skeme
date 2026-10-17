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
(define (min x . rest)
  (let loop ((m x) (l rest))
    (if (null? l) m (loop (if (< (car l) m) (car l) m) (cdr l)))))

(define (max x . rest)
  (let loop ((m x) (l rest))
    (if (null? l) m (loop (if (> (car l) m) (car l) m) (cdr l)))))

;----------------------
; (append-reverse rev tail): the reverse of rev, followed by tail
;----------------------
(define (append-reverse rev tail)
  (if (null? rev) tail (append-reverse (cdr rev) (cons (car rev) tail))))

;----------------------
; (iota count [start [step]]): count numbers from start (0), by step (1)
;----------------------
(define (iota count . args)
  (let ((start (if (null? args) 0 (car args)))
        (step (if (or (null? args) (null? (cdr args))) 1 (car (cdr args)))))
    (let loop ((i (- count 1)) (acc '()))
      (if (< i 0) acc (loop (- i 1) (cons (+ start (* i step)) acc))))))

;----------------------
; (sort list less?): a new list of the elements in order; a stable merge
; sort, so equal elements keep their order
;----------------------
(define (sort lst less?)
  (define (merge a b acc)
    (cond ((null? a) (append-reverse acc b))
          ((null? b) (append-reverse acc a))
          ((less? (car b) (car a)) (merge a (cdr b) (cons (car b) acc)))
          (else (merge (cdr a) b (cons (car a) acc)))))
  (define (msort l n)       ; the first n elements of l, sorted
    (cond ((= n 0) '())
          ((= n 1) (list (car l)))
          (else (let ((half (quotient n 2)))
                  (merge (msort l half) (msort (list-tail l half) (- n half)) '())))))
  (msort lst (length lst)))

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
; assv, memv: find by key using eqv?
;----------------------
(define (assv key alist)
  (cond
    ((null? alist) #f)
    ((eqv? key (car (car alist))) (car alist))
    (else (assv key (cdr alist)))))

(define (memv x lst)
  (cond
    ((null? lst) #f)
    ((eqv? x (car lst)) lst)
    (else (memv x (cdr lst)))))

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
