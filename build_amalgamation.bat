@echo off
setlocal

rem Build only the SQLite/SQLCipher amalgamation source: sqlite3.c.
rem Usage:
rem   build_amalgamation.bat        builds with the x64 MSVC environment
rem   build_amalgamation.bat x86    builds with the x86 MSVC environment
rem   build_amalgamation.bat x64    builds with the x64 MSVC environment

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"

set "TCLDIR=D:\tools\tcl"
set "BISONDIR=D:\tools\bison"
set "PERLDIR=C:\ProgramData\strawberry\perl"
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
if not exist "%PERLDIR%\bin\perl.exe" (
  echo Perl was not found under "%PERLDIR%\bin".
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

set "OUTDIR=%ROOT%\build\%ARCH%\amalgamation"
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
if not exist "%ROOT%\build\%ARCH%" mkdir "%ROOT%\build\%ARCH%"
if exist "%OUTDIR%" rmdir /Q /S "%OUTDIR%"
mkdir "%OUTDIR%"
if errorlevel 1 exit /b %errorlevel%

pushd "%OUTDIR%"
if errorlevel 1 exit /b %errorlevel%

set "PATH=%TCLDIR%\bin;%BISONDIR%;%BISONDIR%\bin;%PERLDIR%\bin;%PATH%"

nmake /f "%ROOT%\Makefile.msc" sqlite3.c ^
  "TOP=%ROOT%" ^
  "TCLDIR=%TCLDIR%" ^
  "NO_TCL=1" ^
  "WITHOUT_JIMSH=1"
if errorlevel 1 (
  popd
  exit /b %errorlevel%
)

popd
echo Generated amalgamation under "%OUTDIR%".
exit /b 0
