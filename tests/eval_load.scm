; loaded by test.scm through eval, to test load inside an eval string
(define lf-loaded 7)
(define (lf-loaded-fn x) (* x lf-loaded))