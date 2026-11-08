@echo off
REM Generic launcher: drag a .vst3 file onto this .bat to open its UI.
REM NOTE: keep this file ASCII-only (see the other .bat for the reason).

if "%~1"=="" (
    echo Usage: drag a .vst3 file onto this .bat
    echo    or:  rea-viewer.bat "C:\path\to\plugin.vst3"
    pause
    exit /b 1
)

"%~dp0rea-viewer.exe" --plugin "%~1"
