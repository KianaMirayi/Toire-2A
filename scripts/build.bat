@echo off
REM ─────────────────────────────────────────────────────────────
REM TOIRE-2A build script
REM   build.bat [Debug|Release] [target]
REM
REM 由 .vscode/tasks.json 调用；也可在命令行直接跑。
REM 用独立的 .bat 是为了避开 VSCode shell 的引号二次解析问题。
REM ─────────────────────────────────────────────────────────────
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo [build] ERROR: vcvarsall.bat not found:
    echo         %VCVARS%
    exit /b 1
)

call "%VCVARS%" x64 >nul
if errorlevel 1 (
    echo [build] ERROR: vcvarsall failed
    exit /b 1
)

set "CFG=%~1"
if "%CFG%"=="" set "CFG=Debug"

set "TGT=%~2"
set "BUILDDIR=%~dp0..\build"

if "%TGT%"=="" (
    echo [build] cmake --build "%BUILDDIR%" --config %CFG%
    cmake --build "%BUILDDIR%" --config %CFG%
) else (
    echo [build] cmake --build "%BUILDDIR%" --config %CFG% --target %TGT%
    cmake --build "%BUILDDIR%" --config %CFG% --target %TGT%
)

exit /b %errorlevel%
