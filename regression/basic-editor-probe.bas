SUB Probe()
    IF ready THEN
        SELECT CASE value
            CASE 1
                PRINT "one"
            CASE ELSE
                PRINT "other"
        END SELECT
    ELSE
        PRINT "not ready"
    END IF
END SUB
