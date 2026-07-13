@echo off
setlocal

rem Build only the SQLite/SQLCipher amalgamation source: sqlite3.c.
rem Usage:
rem   build_amalgamation.bat        builds with the x64 MSVC environment
rem   build_amalgamation.bat x86    builds with the x86 MSVC environment
rem   build_amalgamation.bat x64    builds with the x64 MSVC environment

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"

set "TCLDIR=D:\tools\tcl"
set "BISONDIR=D:\tools\bison"
set "VCVARS32=D:\VisualStudio2019\VC\Auxiliary\Build\vcvars32.bat"
set "VCVARS64=D:\VisualStudio2019\VC\Auxiliary\Build\vcvars64.bat"

if /I "%ARCH%"=="x86" (
  set "VCVARS=%VCVARS32%"
) else if /I "%ARCH%"=="x64" (
  set "VCVARS=%VCVARS64%"
) else (
  echo Unsupported architecture "%ARCH%". Use x86 or x64.
  exit /b 2
)

if not exist "%VCVARS%" (
  echo MSVC environment script not found: "%VCVARS%"
  exit /b 1
)

if not exist "%TCLDIR%\bin\tclsh.exe" (
  if not exist "%TCLDIR%\bin\tclsh86.exe" (
    if not exist "%TCLDIR%\bin\tclsh90.exe" (
      echo Tcl shell was not found under "%TCLDIR%\bin".
      exit /b 1
    )
  )
)

call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%

set "PATH=%TCLDIR%\bin;%BISONDIR%;%BISONDIR%\bin;%PATH%"

nmake /f Makefile.msc sqlite3.c ^
  "TCLDIR=%TCLDIR%" ^
  "NO_TCL=1" ^
  "WITHOUT_JIMSH=1"
if errorlevel 1 exit /b %errorlevel%

rem Keep the generated amalgamation sources and remove build-only tools.
del /Q *.exe *.obj *.ilk *.pdb 2>NUL
del /Q parse.c parse.h parse.out parse.sql parse.y 2>NUL
del /Q fts5parse.c fts5parse.h fts5parse.out fts5parse.sql fts5parse.y 2>NUL
del /Q ctime.c fts5.c fts5.h keywordhash.h lempar.c opcodes.c opcodes.h pragma.h shell.c sqlite3session.h tclsqlite-ex.c 2>NUL
if exist tsrc rmdir /Q /S tsrc

echo Generated sqlite3.c and headers.
exit /b 0
