@echo off
setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "PLATFORM_NAME=%PLATFORM%"
if "%PLATFORM_NAME%"=="" set "PLATFORM_NAME=x64"

set "OUTDIR=%ROOT%\build\%PLATFORM_NAME%\make"
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
if not exist "%ROOT%\build\%PLATFORM_NAME%" mkdir "%ROOT%\build\%PLATFORM_NAME%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

pushd "%OUTDIR%"
if errorlevel 1 exit /b %errorlevel%

nmake /f "%ROOT%\Makefile.msc" "TOP=%ROOT%" "TCLDIR=D:\tools\tcl" %*
set "MAKE_EXIT=%ERRORLEVEL%"

popd
exit /b %MAKE_EXIT%
