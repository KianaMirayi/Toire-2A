@echo off
REM ─────────────────────────────────────────────────────────────
REM TOIRE-2A configure script
REM   configure.bat
REM ─────────────────────────────────────────────────────────────
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo [configure] ERROR: vcvarsall.bat not found:
    echo             %VCVARS%
    exit /b 1
)

call "%VCVARS%" x64 >nul
if errorlevel 1 (
    echo [configure] ERROR: vcvarsall failed
    exit /b 1
)

set "ROOT=%~dp0.."
echo [configure] cmake -S "%ROOT%" -B "%ROOT%\build" -G "Visual Studio 18 2026" -A x64
cmake -S "%ROOT%" -B "%ROOT%\build" -G "Visual Studio 18 2026" -A x64

exit /b %errorlevel%
