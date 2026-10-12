@echo off
rem Build with MSVC (no CMake needed). Run from any cmd window -
rem the script locates Visual Studio via vswhere if cl.exe is not in PATH.
setlocal
where cl >nul 2>nul
if not errorlevel 1 goto build
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
:build
if not exist build mkdir build
set FLAGS=/nologo /EHsc /std:c++17 /W4 /utf-8 /O2
cl %FLAGS% src\server\udp_server.cpp   /Fobuild\ /Febuild\udp_server.exe   ws2_32.lib || exit /b 1
cl %FLAGS% src\client\udp_client.cpp   /Fobuild\ /Febuild\udp_client.exe   ws2_32.lib || exit /b 1
cl %FLAGS% src\echo\echo_server.cpp    /Fobuild\ /Febuild\echo_server.exe  ws2_32.lib || exit /b 1
cl %FLAGS% src\echo\echo_client.cpp    /Fobuild\ /Febuild\echo_client.exe  ws2_32.lib || exit /b 1
cl %FLAGS% tests\test_protocol.cpp     /Fobuild\ /Febuild\test_protocol.exe           || exit /b 1
echo.
echo Done: build\udp_server.exe, build\udp_client.exe, build\echo_*.exe, build\test_protocol.exe
