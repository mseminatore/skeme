; error_test.scm - Demonstrates unified error reporting

; Test 1: Basic display and newline output
(display "Testing unified error reporting...")
(newline)

; Test 2: This should work fine
(define x 42)
(display "x = ")
(display x)
(newline)

; Test 3: Arithmetic operations
(display "Result: ")
(display (+ 1 2 3))
(newline)

; Test 4: List operations
(display "List: ")
(display (list 1 2 3))
(newline)

(display "All tests completed!")
(newline)
