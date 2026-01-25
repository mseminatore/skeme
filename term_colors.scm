;-----------------------------------------------
; Skeme terminal color definitions
;
; Copyright (C) 2026 by Mark Seminatore
;-----------------------------------------------

;---------------------------
; terminal control sequences
;---------------------------
(define ESC "\e")
(define TERM_CLEAR (string-append ESC "[2J" ESC "[H"))
(define TERM_RESET (string-append ESC "[0m"))
(define TERM_RED (string-append ESC "[31m"))
(define TERM_GREEN (string-append ESC "[32m"))
(define TERM_MAGENTA (string-append ESC "[35m"))
