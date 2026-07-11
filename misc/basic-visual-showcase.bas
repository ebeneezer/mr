' BASIC visual verification fixture for MR.
'
' This file intentionally combines FreeBASIC, Gambas and QB64 constructs.
' It is for coloring, folding and smart-indentation inspection only; use
' regression/basic-smoke.bas for a FreeBASIC compiler smoke test.

#INCLUDE ONCE "visual-showcase.bi"
$IF VISUAL_TEST THEN
$END IF

REM Full-line REM comments and apostrophe comments must both be colored.
CONST GoldenRatio = PI
DIM decimalValue AS DOUBLE = 3.14159E-2
DIM hexadecimalValue AS ULONG = &HFEED
DIM octalValue AS UINTEGER = &O755
DIM binaryValue AS UBYTE = &B10101010
DIM integerValue AS INTEGER = 42%
DIM longValue AS LONG = 42&
DIM singleValue AS SINGLE = 1.5!
DIM doubleValue AS DOUBLE = 2.5#
DIM message AS STRING = "He said ""BASIC""."
DIM wideMessage AS WSTRING * 32
DIM pointerValue AS POINTER = NULL
DIM state AS BOOLEAN = TRUE
DIM objectValue AS OBJECT = NOTHING
DIM variantValue AS VARIANT
DIM anyValue AS ANY
DIM byteValue AS BYTE, shortValue AS SHORT, longIntValue AS LONGINT
DIM longPtrValue AS LONGPTR, uByteValue AS UBYTE, uShortValue AS USHORT
DIM uLongValue AS ULONG, uLongIntValue AS ULONGINT, uIntegerValue AS UINTEGER
DIM qbBit AS _BIT, qbByte AS _BYTE, qbFloat AS _FLOAT, qbInteger AS _INTEGER
DIM qbMemory AS _MEM, qbOffset AS _OFFSET, qbUnsigned AS _UNSIGNED

TYPE VisualRecord
    name AS STRING
    score AS DOUBLE
END TYPE

ENUM VisualLevel
    LevelLow = 1
    LevelHigh = 9
END ENUM

STRUCT VisualStruct
    x AS SINGLE
    y AS SINGLE
END STRUCT

PUBLIC CLASS VisualClass
    PRIVATE label AS STRING

    CONSTRUCTOR VisualClass()
        label = "new"
    END CONSTRUCTOR

    DESTRUCTOR VisualClass()
        label = "deleted"
    END DESTRUCTOR

    PUBLIC PROPERTY Caption AS STRING
        label = "caption"
    END PROPERTY
END CLASS

DECLARE SUB ShowLegacyForms(BYREF record AS VisualRecord)
DECLARE FUNCTION ComputeScore(BYVAL value AS INTEGER) AS DOUBLE

FUNCTION ComputeScore(BYVAL value AS INTEGER) AS DOUBLE
    DIM result AS DOUBLE

    result = ABS(value) MOD 7
    result = result + ASC("A")
    IF value > 0 AND NOT FALSE OR TRUE XOR FALSE IMP FALSE THEN
        RETURN result
    ELSEIF value = 0 THEN
        RETURN GoldenRatio
    ELSE IF value < -100 THEN
        RETURN -1
    ELSE
        RETURN 0
    END IF
END FUNCTION

SUB ShowLegacyForms(BYREF record AS VisualRecord)
    STATIC callCount AS INTEGER
    SHARED sharedName AS STRING
    DIM index AS INTEGER
    DIM item AS INTEGER

    LET record.name = "visual"
    REDIM PRESERVE values(1 TO 3) AS INTEGER
    ERASE values
    RANDOMIZE
    BEEP
    LOCATE 1, 1
    PRINT USING "###.##"; record.score
    REM A REM comment after a statement start must consume the rest of its line.

    IF state THEN
        PRINT "if block"
    ENDIF

    SELECT CASE integerValue
        CASE 0
            PRINT "zero"
        CASE 1 TO 9
            PRINT "single digit"
        CASE ELSE
            PRINT "other"
    END SELECT

    FOR index = 0 TO 10 STEP 2
        IF index = 4 THEN
            CONTINUE FOR
        END IF
        PRINT index
    NEXT index

    FOR EACH item IN values()
        PRINT item
    NEXT

    DO
        integerValue += 1
    LOOP UNTIL integerValue > 3

    WHILE integerValue < 10
        integerValue += 1
    WEND

    WHILE integerValue < 12
        integerValue += 1
    END WHILE

    WITH record
        .score = ComputeScore(integerValue)
        .name = "record"
    END WITH

    TRY
        CALL ShowLegacyForms(record)
    CATCH errorCode AS INTEGER
        ERROR errorCode
    FINALLY
        PRINT "cleanup"
    END TRY

    EXIT SUB
END SUB

' Legacy, line-numbered BASIC constructs.
100 DATA 10, 20, 30
110 READ integerValue
120 RESTORE 100
130 ON integerValue GOTO 200, 300
140 GOSUB 900
150 OPEN "visual.dat" FOR RANDOM AS #1
160 INPUT "Value"; integerValue
170 LINE INPUT message
180 GOTO 400
200 DEF FNDouble(value) = value * 2
300 STOP
400 RETURN
900 RETURN

PUBLIC SUB GambasStyleProcedure()
    PRINT "END without a suffix closes this procedure"
END
