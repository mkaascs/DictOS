@echo off
echo === DictOS Build ===

echo [1/3] Compiling kernel...
cl.exe /GS- /c /nologo kernel.cpp
if errorlevel 1 ( echo COMPILE FAILED & pause & exit /b 1 )

echo [2/3] Linking kernel...
link.exe /OUT:kernel.bin /BASE:0x10000 /FIXED /FILEALIGN:512 ^
    /MERGE:.rdata=.data /IGNORE:4254 /NODEFAULTLIB ^
    /ENTRY:startup /SUBSYSTEM:NATIVE /nologo ^
    kernel.obj
if errorlevel 1 ( echo LINK FAILED & pause & exit /b 1 )

echo [3/3] Assembling bootloader...
fasm bootsect.asm bootsect.bin
if errorlevel 1 ( echo FASM FAILED & pause & exit /b 1 )

echo.
echo === dumpbin - READ THESE VALUES AND UPDATE bootsect.asm! ===
dumpbin /headers kernel.bin | findstr /i "virtual address size of raw file pointer"
echo.
echo Formula: CL = (file pointer) / 512 + 1
echo          AL = (size of raw data) / 512  (round up)
echo          .text always loads to 0x11000, .data to 0x12000
echo.
echo === kernel.bin size ===
for %%F in (kernel.bin) do echo %%~zF bytes
echo.
echo Run: qemu-system-i386 -fda bootsect.bin -fdb kernel.bin
pause