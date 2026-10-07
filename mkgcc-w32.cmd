@echo off
rem Note: This file must be saved with GBK/ANSI encoding and CRLF line endings
setlocal
set "MSYS2_ROOT=G:\msys64-20241208"
set "MSYSTEM=MINGW32"
set "MINGW_PREFIX=%MSYS2_ROOT:\=/%/mingw32"

set "PATH=%MSYS2_ROOT%\mingw32\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
set NOWERROR=1
set LDOPTS=-fuse-ld=lld
set OSD=messui
make -j3 %*

set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo === BUILD OK ===) else (echo === BUILD FAILED, rc=%RC% ===)
endlocal & timeout -t 10 & exit /b %RC%