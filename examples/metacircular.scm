; metacircular.scm - a Scheme interpreter written in Skeme.
;
; A small SICP-style evaluator: numbers, strings, booleans, quote, if, cond,
; define, set!, lambda, let, begin, and a handful of primitives.
; It stresses recursion, symbols, association lists and the garbage collector.
;
; Run:  ./skeme examples/metacircular.scm
;
; Style notes (Skeme quirks this file works around):
;  * Skeme compiles in a single pass, so a global must exist before anything
;    refers to it. The mutually recursive pair m-eval / m-apply is
;    forward-declared and filled in with set! further down.
;  * This file avoids `let` in the evaluator itself. A Skeme closure that
;    calls another function and then reads a captured variable loses its
;    upvalues, and `let` is a closure over the enclosing locals. Helper
;    functions that take the value as a parameter do the same job safely.
;  * The words quote, true and false are lexer keywords, so the symbols for
;    them are built with string->symbol.

(load "stdlib.scm")

; ---- helpers (Skeme has no caddr & friends) --------------------------------
(define (caddr x) (car (cddr x)))
(define (cdddr x) (cdr (cddr x)))
(define (cadddr x) (car (cdddr x)))

; ---- special form names, as symbols ----------------------------------------
(define s-quote  (string->symbol "quote"))
(define s-if     (string->symbol "if"))
(define s-cond   (string->symbol "cond"))
(define s-else   (string->symbol "else"))
(define s-define (string->symbol "define"))
(define s-set    (string->symbol "set!"))
(define s-lambda (string->symbol "lambda"))
(define s-let    (string->symbol "let"))
(define s-begin  (string->symbol "begin"))

; ---- environments ----------------------------------------------------------
; An environment is a list of frames. A frame is (frame . bindings), where
; bindings is an association list of (name . value). The frame head lets us
; add new bindings in place with set-cdr!.
(define (make-bindings names vals)
  (if (null? names)
      (quote ())
      (cons (cons (car names) (car vals))
            (make-bindings (cdr names) (cdr vals)))))

(define (extend-env names vals env)
  (cons (cons (quote frame) (make-bindings names vals)) env))

; lookup-binding and binding-or-outer call each other: forward-declare one.
(define lookup-binding #f)

(define (binding-or-outer name b env)
  (if b b (lookup-binding name (cdr env))))

(set! lookup-binding (lambda (name env)
  (if (null? env)
      #f
      (binding-or-outer name (assq name (cdr (car env))) env))))

(define (binding-value b)
  (if b (cdr b) (error "unbound variable")))

(define (lookup name env)
  (binding-value (lookup-binding name env)))

(define (add-binding! frame name val)
  (set-cdr! frame (cons (cons name val) (cdr frame))))

(define (define-var! name val env)
  (add-binding! (car env) name val))

(define (set-binding! b val)
  (if b (begin (set-cdr! b val) val) (error "set!: unbound variable")))

(define (set-var! name val env)
  (set-binding! (lookup-binding name env) val))

; ---- procedures ------------------------------------------------------------
(define (make-closure params body env)
  (list (quote closure) params body env))
(define (closure? f) (if (pair? f) (eq? (car f) (quote closure)) #f))
(define (closure-params f) (cadr f))
(define (closure-body f)   (caddr f))
(define (closure-env f)    (cadddr f))

(define (primitive? f) (if (pair? f) (eq? (car f) (quote primitive)) #f))
(define (prim-name f) (cadr f))

; Skeme has no apply, so primitives dispatch by name.
(define (prim2 name a b)
  (cond ((eq? name (quote +))     (+ a b))
        ((eq? name (quote -))     (- a b))
        ((eq? name (quote *))     (* a b))
        ((eq? name (quote /))     (/ a b))
        ((eq? name (quote =))     (= a b))
        ((eq? name (quote <))     (< a b))
        ((eq? name (quote >))     (> a b))
        ((eq? name (quote car))   (car a))
        ((eq? name (quote cdr))   (cdr a))
        ((eq? name (quote cons))  (cons a b))
        ((eq? name (quote null?)) (null? a))
        ((eq? name (quote pair?)) (pair? a))
        ((eq? name (quote eq?))   (eq? a b))
        ((eq? name (quote not))   (not a))
        (else (error "unknown primitive"))))

(define (apply-primitive name args)
  (prim2 name (car args) (if (null? (cdr args)) #f (cadr args))))

; ---- the evaluator ---------------------------------------------------------
(define m-eval #f)
(define m-apply #f)

(define (eval-args exps env)
  (if (null? exps)
      (quote ())
      (cons (m-eval (car exps) env)
            (eval-args (cdr exps) env))))

(define (eval-seq exps env)
  (if (null? (cdr exps))
      (m-eval (car exps) env)
      (begin (m-eval (car exps) env)
             (eval-seq (cdr exps) env))))

(define (eval-if exp env)
  (if (m-eval (cadr exp) env)
      (m-eval (caddr exp) env)
      (if (null? (cdddr exp))
          #f
          (m-eval (cadddr exp) env))))

; eval-cond and eval-clause call each other: forward-declare one.
(define eval-cond #f)

(define (eval-clause clause clauses env)
  (if (eq? (car clause) s-else)
      (eval-seq (cdr clause) env)
      (if (m-eval (car clause) env)
          (eval-seq (cdr clause) env)
          (eval-cond (cdr clauses) env))))

(set! eval-cond (lambda (clauses env)
  (if (null? clauses)
      #f
      (eval-clause (car clauses) clauses env))))

; define returns the defined name (returning the frame would print forever:
; a closure's environment contains the closure).
(define (eval-define-target target exp env)
  (if (pair? target)
      ; (define (name . params) body...)
      (begin (define-var! (car target)
                          (make-closure (cdr target) (cddr exp) env)
                          env)
             (car target))
      (begin (define-var! target (m-eval (caddr exp) env) env)
             target)))

(define (eval-define exp env)
  (eval-define-target (cadr exp) exp env))

; (let ((name expr) ...) body...)
(define (eval-let-bindings bindings exp env)
  (eval-seq (cddr exp)
            (extend-env (map car bindings)
                        (eval-args (map cadr bindings) env)
                        env)))

(define (eval-let exp env)
  (eval-let-bindings (cadr exp) exp env))

(define (eval-form head exp env)
  (cond ((eq? head s-quote)  (cadr exp))
        ((eq? head s-if)     (eval-if exp env))
        ((eq? head s-cond)   (eval-cond (cdr exp) env))
        ((eq? head s-define) (eval-define exp env))
        ((eq? head s-set)    (set-var! (cadr exp) (m-eval (caddr exp) env) env))
        ((eq? head s-lambda) (make-closure (cadr exp) (cddr exp) env))
        ((eq? head s-let)    (eval-let exp env))
        ((eq? head s-begin)  (eval-seq (cdr exp) env))
        (else (m-apply (m-eval head env)
                       (eval-args (cdr exp) env)))))

(set! m-apply (lambda (f args)
  (cond ((primitive? f) (apply-primitive (prim-name f) args))
        ((closure? f)
         (eval-seq (closure-body f)
                   (extend-env (closure-params f) args (closure-env f))))
        (else (error "not a procedure")))))

(set! m-eval (lambda (exp env)
  (cond ((number? exp)  exp)
        ((string? exp)  exp)
        ((boolean? exp) exp)
        ((symbol? exp)  (lookup exp env))
        ((pair? exp)    (eval-form (car exp) exp env))
        (else (error "cannot evaluate")))))

; ---- global environment ----------------------------------------------------
(define (define-prims! names env)
  (if (null? names)
      #t
      (begin
        (define-var! (car names) (list (quote primitive) (car names)) env)
        (define-prims! (cdr names) env))))

(define (init-env env)
  (define-prims! (quote (+ - * / = < > car cdr cons null? pair? eq? not)) env)
  (define-var! (string->symbol "true") #t env)
  (define-var! (string->symbol "false") #f env)
  env)

(define global-env (init-env (extend-env (quote ()) (quote ()) (quote ()))))

; ---- demo ------------------------------------------------------------------
(define (run label exp)
  (display label)
  (display " => ")
  (display (m-eval exp global-env))
  (newline))

; Everything runs from one function so the script prints a single
; top-level result instead of echoing every expression.
(define (demo)
  (display "Metacircular evaluator running on Skeme")
  (newline)

  (run "arithmetic       " (quote (+ 1 (* 2 3))))
  (run "if               " (quote (if (< 1 2) "yes" "no")))
  (run "let              " (quote (let ((x 5) (y 7)) (* x y))))
  (run "define fact      " (quote (define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))))
  (run "(fact 10)        " (quote (fact 10)))
  (run "define fib       " (quote (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))))
  (run "(fib 15)         " (quote (fib 15)))

  ; closures capture their environment
  (run "define make-adder" (quote (define (make-adder n) (lambda (x) (+ x n)))))
  (run "define add5      " (quote (define add5 (make-adder 5))))
  (run "(add5 10)        " (quote (add5 10)))

  ; higher-order functions and lists
  (run "define map       " (quote (define (map f xs) (if (null? xs) (quote ()) (cons (f (car xs)) (map f (cdr xs)))))))
  (run "map add5 list    " (quote (map add5 (quote (1 2 3)))))

  ; mutation through set!
  (run "define counter   " (quote (define (make-counter) (let ((n 0)) (lambda () (begin (set! n (+ n 1)) n))))))
  (run "define c         " (quote (define c (make-counter))))
  (run "c c c            " (quote (cond ((= (c) 1) (c) (c)) (else 0))))

  ; Skeme's stack is 1024 slots by default, which fits roughly 20 nested
  ; interpreted calls. For deeper runs build with a bigger stack, e.g.
  ;   make CFLAGS="-I. -I./hash/testy -O2 -DVM_STACK_SIZE=65536"
  (run "define count     " (quote (define (count i) (if (< i 15) (count (+ i 1)) i))))
  (run "(count 0)        " (quote (count 0)))
  #t)

(demo)
