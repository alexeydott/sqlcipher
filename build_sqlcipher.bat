@echo off
setlocal EnableDelayedExpansion

rem =========================================================================
rem  build_sqlcipher.bat - Compile SQLCipher DLL or static lib
rem
rem  Prerequisite: sqlite3.c must already exist.
rem                Run build_amalgamation.bat to regenerate it if needed.
rem
rem  Usage:
rem    build_sqlcipher.bat [arch] [provider] [target]
rem
rem  arch:
rem    x64           64-bit (default)
rem    x86           32-bit
rem
rem  provider:
rem    static        CNG - link bcrypt.lib (default)
rem    dynamic       CNG - LoadLibrary("bcrypt.dll"), no import lib
rem
rem  target:
rem    dll           sqlite3.dll + sqlite3.lib import lib (default)
rem    lib           sqlite3.lib static library
rem
rem  Examples:
rem    build_sqlcipher.bat
rem    build_sqlcipher.bat x64 static dll
rem    build_sqlcipher.bat x64 dynamic dll
rem    build_sqlcipher.bat x86 static lib
rem =========================================================================

rem --- Paths ---
set "VCVARS32=D:\VisualStudio2019\VC\Auxiliary\Build\vcvars32.bat"
set "VCVARS64=D:\VisualStudio2019\VC\Auxiliary\Build\vcvars64.bat"
set "ZLIBDIR=D:\projects\externals\gdal3\deps\src\zlib-1.3.1"

rem --- Defaults ---
set "ARCH=x64"
set "PROVIDER=static"
set "TARGET=dll"

rem --- Parse arguments ---
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="x64"     set "ARCH=x64"       & shift & goto parse_args
if /I "%~1"=="x86"     set "ARCH=x86"       & shift & goto parse_args
if /I "%~1"=="static"  set "PROVIDER=static" & shift & goto parse_args
if /I "%~1"=="dynamic" set "PROVIDER=dynamic" & shift & goto parse_args
if /I "%~1"=="dll"     set "TARGET=dll"     & shift & goto parse_args
if /I "%~1"=="lib"     set "TARGET=lib"     & shift & goto parse_args
echo Unknown argument: %~1
goto usage
:args_done

rem --- Resolve MSVC environment ---
if /I "%ARCH%"=="x86" (
    set "VCVARS=%VCVARS32%"
    set "MACHINE=x86"
) else (
    set "VCVARS=%VCVARS64%"
    set "MACHINE=x64"
)

rem --- Validate ---
if not exist "%VCVARS%" (
    echo ERROR: MSVC not found: %VCVARS%
    exit /b 1
)
if not exist sqlite3.c (
    echo ERROR: sqlite3.c not found. Run build_amalgamation.bat first.
    exit /b 1
)
if not exist "%ZLIBDIR%\zlib.h" (
    echo ERROR: zlib sources not found: %ZLIBDIR%
    exit /b 1
)
if not exist zconf.h (
    if exist "%ZLIBDIR%\zconf.h.included" (
        copy /Y "%ZLIBDIR%\zconf.h.included" zconf.h >NUL
    )
)
if not exist zconf.h (
    echo ERROR: zconf.h not found. Expected "%ZLIBDIR%\zconf.h.included".
    exit /b 1
)

rem --- Provider flags ---
if /I "%PROVIDER%"=="dynamic" (
    set "PROVIDER_FLAGS=-DSQLCIPHER_CRYPTO_CNG -DSQLCIPHER_CRYPTO_CNG_DYNAMIC"
    set "EXTRA_LIBS="
    set "PROVIDER_DESC=CNG dynamic (LoadLibrary)"
    rem Dynamic provider uses dynamic CRT (/MD)
    set "CRT_FLAG=/MD"
    set "NODEFAULTLIB_FLAGS="
) else (
    set "PROVIDER_FLAGS=-DSQLCIPHER_CRYPTO_CNG"
    set "EXTRA_LIBS=bcrypt.lib"
    set "PROVIDER_DESC=CNG static (bcrypt.lib)"
    rem Static provider uses static CRT (/MT) - embed CRT into the binary
    set "CRT_FLAG=/MT"
    set "NODEFAULTLIB_FLAGS=/NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:vcruntime.lib /NODEFAULTLIB:ucrt.lib"
)

rem --- Compiler flags ---
set "CFLAGS=-nologo -W3 -O2 !CRT_FLAG! -D_CRT_SECURE_NO_WARNINGS"
set "CFLAGS=%CFLAGS% -DSQLITE_HAS_CODEC %PROVIDER_FLAGS%"
set "CFLAGS=%CFLAGS% -DSQLITE_EXTRA_INIT=sqlcipher_extra_init"
set "CFLAGS=%CFLAGS% -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown"
set "CFLAGS=%CFLAGS% -DSQLITE_TEMP_STORE=2"
set "CFLAGS=%CFLAGS% -DSQLITE_THREADSAFE=1"
set "CFLAGS=%CFLAGS% -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_RTREE"
set "CFLAGS=%CFLAGS% -DSQLITE_ENABLE_COLUMN_METADATA"
set "CFLAGS=%CFLAGS% -DSQLITE_HAVE_ZLIB=1 -I. -I%ZLIBDIR%"

echo.
echo =========================================================================
echo  SQLCipher build
echo    Architecture : %ARCH%
echo    Provider     : %PROVIDER_DESC%
echo    Target       : %TARGET%
echo =========================================================================
echo.

call "%VCVARS%"
if errorlevel 1 goto vcvars_failed

rem DLL export flag - stored separately so delayed expansion avoids ( ) issues
set "DLL_EXPORT=-DSQLITE_API=__declspec(dllexport)"

rem --- Compile zlib sources needed by zipfile.c ---
set "ZLIB_OBJS="
for %%S in (
    adler32.c
    compress.c
    crc32.c
    deflate.c
    infback.c
    inffast.c
    inftrees.c
    inflate.c
    trees.c
    uncompr.c
    zutil.c
) do (
    set "ZLIB_OBJ=zlib_%%~nS.obj"
    echo Compiling zlib %%S...
    cl -nologo -W3 -O2 !CRT_FLAG! -D_CRT_SECURE_NO_WARNINGS -I. -I"%ZLIBDIR%" /c "%ZLIBDIR%\%%S" /Fo"!ZLIB_OBJ!"
    if errorlevel 1 goto zlib_failed
    set "ZLIB_OBJS=!ZLIB_OBJS! !ZLIB_OBJ!"
)

rem --- Compile sqlite3.c to sqlite3.obj ---
echo Compiling sqlite3.c...
if /I "%TARGET%"=="dll" (
    cl %CFLAGS% !DLL_EXPORT! /c sqlite3.c /Fo:sqlite3.obj
) else (
    cl %CFLAGS% /c sqlite3.c /Fo:sqlite3.obj
)
if errorlevel 1 ( echo ERROR: Compile failed. & exit /b 1 )
echo OK: sqlite3.obj

if /I "%TARGET%"=="dll" (
    echo Linking sqlite3.dll...
    link /DLL /NOLOGO /MACHINE:%MACHINE% /OUT:sqlite3.dll sqlite3.obj !ZLIB_OBJS! %EXTRA_LIBS% !NODEFAULTLIB_FLAGS!
    if errorlevel 1 ( echo ERROR: Link failed. & exit /b 1 )
    echo OK: sqlite3.dll
) else (
    echo Building sqlite3.lib...
    lib /NOLOGO /MACHINE:%MACHINE% /OUT:sqlite3.lib sqlite3.obj !ZLIB_OBJS!
    if errorlevel 1 ( echo ERROR: lib failed. & exit /b 1 )
    echo OK: sqlite3.lib
)

echo.
echo =========================================================================
echo  DONE
echo =========================================================================
exit /b 0

:usage
echo.
echo Usage: build_sqlcipher.bat [x64^|x86] [static^|dynamic] [dll^|lib]
echo.
echo   build_sqlcipher.bat                  x64, CNG static, DLL
echo   build_sqlcipher.bat x64 dynamic dll  x64, CNG dynamic, DLL
echo   build_sqlcipher.bat x86 static lib   x86, CNG static, static lib
exit /b 2

:vcvars_failed
echo ERROR: Failed to initialise MSVC environment.
exit /b 1

:zlib_failed
echo ERROR: zlib compile failed.
exit /b 1
