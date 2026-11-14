@echo off
rem Add the toolchain paths to PATH for the current terminal window.
rem (They are also added to the user PATH permanently; restart VS Code or
rem  any open terminals to pick that up. Use this script for already-open
rem  terminals.)
set "PATH=C:\Code\tools\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin;C:\Code\tools\xpack-openocd-0.12.0-7\bin;C:\Program Files\Git\usr\bin;%PATH%"
echo Toolchain ready:
arm-none-eabi-gcc --version
openocd --version
