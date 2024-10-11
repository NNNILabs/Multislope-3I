@echo off
set loop=0

echo upload start

echo resetting the pico
mode com3 BAUD=1200 

:loop

copy build\*.uf2 E:\
echo %errorlevel%
if "%errorlevel%"=="0" goto success

set /a loop=%loop%+1 
if "%loop%"=="3" goto fail

timeout 1 > NUL
goto loop

:fail
echo failed to program
timeout 1 > NUL
exit

:success
echo pico programmed
timeout 1 > NUL
exit