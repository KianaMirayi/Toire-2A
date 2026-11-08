@echo off
REM Open the UADx LA-2A plugin UI with the rea-viewer host.
REM NOTE: keep this file ASCII-only. cmd reads .bat as the OEM codepage;
REM       UTF-8 Chinese comments can swallow the following line.

"%~dp0rea-viewer.exe" --plugin "C:\Program Files\Common Files\VST3\uaudio_teletronix_la-2a_tc.vst3\Contents\x86_64-win\uaudio_teletronix_la-2a_tc.vst3"
