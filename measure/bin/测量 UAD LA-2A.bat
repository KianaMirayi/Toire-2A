@echo off
REM Open the merged measurement host (live log window) for UADx LA-2A.
REM Keep this file ASCII-only.

set UAD=C:\Program Files\Common Files\VST3\uaudio_teletronix_la-2a_tc.vst3\Contents\x86_64-win\uaudio_teletronix_la-2a_tc.vst3
set SIG=%~dp0..\signals\01-io-stairs.wav
set OUT=%~dp0..\renders\R1-comp--01-io-stairs.wav

if not exist "%~dp0..\renders" mkdir "%~dp0..\renders"

"%~dp0rea-measure.exe" --plugin "%UAD%" --in "%SIG%" --out "%OUT%" --param 0=0.5 --param 1=0.5 --param 2=0 --param 4=1
