@ECHO OFF

IF NOT EXIST help\me.hlp GOTO j1
UTILS\HELPCX help\me.hlp
IF ERRORLEVEL 1 GOTO J1
DEL help\me.hlp

:J1
IF NOT EXIST help\aci_hype.hlp GOTO j2
UTILS\HELPCX help\aci_hype.hlp
IF ERRORLEVEL 1 GOTO J2
DEL help\aci_hype.hlp

:J2
IF NOT EXIST help\dirshell.hlp GOTO j3
UTILS\HELPCX help\dirshell.hlp
IF ERRORLEVEL 1 GOTO J3
DEL help\dirshell.hlp

:J3
:COMPILE
CMAC.EXE src\MESYS.S -Pmac
CMAC.EXE src\WINDOW.S -Pmac
CMAC.EXE src\RESOURCE.S -Pmac
CMAC.EXE src\MOUSE.S -Pmac
CMAC.EXE src\MEUTIL1.S -Pmac
CMAC.EXE src\MEUTIL2.S -Pmac
CMAC.EXE src\MEUTIL3.S -Pmac
CMAC.EXE src\USERIN.S -Pmac
CMAC.EXE src\MEERROR.S -Pmac
CMAC.EXE src\EXIT.S -Pmac
CMAC.EXE src\TEXT.S -Pmac
CMAC.EXE src\KEYMAC.S -Pmac
CMAC.EXE src\DIRSHELL.S -Pmac
CMAC.EXE src\LANGUAGE.S -Pmac

CMAC.EXE src\SETUP.S -Pmac

CMAC.EXE src\SUPPORT.S -Pmac
CMAC.EXE src\WIN3.S -Pmac
CMAC.EXE src\HEX.S -Pmac
CMAC.EXE src\FC.S -Pmac
CMAC.EXE src\C.S -Pmac
CMAC.EXE src\CMAC.S -Pmac
CMAC.EXE src\MEHELP.S -Pmac
CMAC.EXE src\SESSMGR.S -Pmac
CMAC.EXE src\MODINST.S -Pmac
CMAC.EXE src\CALC.S -Pmac
CMAC.EXE src\STATSET.S -Pmac
CMAC.EXE src\NOTEBOOK.S -Pmac
CMAC.EXE src\RESEDIT.S -Pmac

UTILS\memcl.bat NOSPLIT
