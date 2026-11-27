@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl @"G:\GitHub\SecondLife\opensim\native-port\parity-audit\probe\windows-link.rsp"
