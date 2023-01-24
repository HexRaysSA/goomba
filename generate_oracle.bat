@if "%DEBUG%" == "" @echo off
@rem ##########################################################################
@rem
@rem  gooMBA oracle file generation script
@rem
@rem ##########################################################################
@rem Set local scope for the variables with windows NT shell
if "%OS%"=="Windows_NT" setlocal

if .%1 == . goto usage
set VD_MSYNTH_PATH=%~f1
echo generating minsns file (step 1/2)...
idat64 -A -Llog.txt tests/idb/mba_challenge.i64
set VD_MSYNTH_PATH=
set VD_MBA_MINSNS_PATH=%~dpnx1.b
echo generating oracle file (step 2/2)...
idat64 -A -Llog.txt tests/idb/mba_challenge.i64
echo. >> log.txt
echo finished!
move %~dpnx1.b.c %~dpn1.oracle
echo finished! Result is in %~dpn1.oracle
tail log.txt
exit /b
:usage
echo "Usage: generate_oracle.bat all_combined.txt"
