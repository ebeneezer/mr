' FreeBASIC editor and compiler smoke test.

FUNCTION Factorial(BYVAL number AS INTEGER) AS INTEGER
    DIM result AS INTEGER = 1
    DIM index AS INTEGER

    FOR index = 2 TO number
        result *= index
    NEXT

    RETURN result
END FUNCTION

SUB PrintClassification(BYVAL value AS INTEGER)
    IF value < 0 THEN
        PRINT "negative"
    ELSEIF value = 0 THEN
        PRINT "zero"
    ELSE
        SELECT CASE value
            CASE 1 TO 9
                PRINT "single digit"
            CASE ELSE
                PRINT "multiple digits"
        END SELECT
    END IF
END SUB

DIM value AS INTEGER = 5

PrintClassification(value)
PRINT "factorial(" & LTRIM(STR(value)) & ") = " & LTRIM(STR(Factorial(value)))
