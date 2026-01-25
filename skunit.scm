;-----------------------------------------------
; Skeme unit test library functions
;
; Copyright (C) 2026 by Mark Seminatore
;-----------------------------------------------

;(if (null? TERM_RESET) 
;    (error "term_colors.scm")
;)

;----------------------
; define test case runner
;----------------------
(define err_count 0)
(define test_count 0)
(define test-pass-msg (string-append TERM_GREEN "OK " TERM_RESET))
(define test-fail-msg (string-append TERM_RED "failed!" TERM_RESET))

(define (test-report-error)
    (display test-fail-msg)
    (set! err_count (+ 1 err_count))
)

(define (test-case name test-fn)
  (display (string-append "\t" (number->string (+ 1 test_count)) " test case: " TERM_MAGENTA name "..."))
  (if test-fn (display test-pass-msg) (test-report-error))
;  (newline)
  (set! test_count (+ 1 test_count))
  (not (not test-fn))
)

(define (test-suite str)
    (display "Testing suite: ")
    (display str)
)

(define (test-end)
    (gc-stats)
    (newline)
    (display test_count)
    (display " tests passed successfully, ")
    (display err_count)
    (display " failures.")
    (newline)
    ;(if err_count (display (string-append err_count " tests failed!")))
    (if (not interactive) (exit err_count))
)
