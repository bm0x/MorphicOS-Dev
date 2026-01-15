@echo off
setlocal EnableExtensions EnableDelayedExpansion
echo ========================================
echo Morphic OS - Windows Debug Mode
echo ========================================
echo Waiting for GDB connection on localhost:1234...
echo Serial output will appear in this terminal.
echo.

REM Path to QEMU
set "QEMU=C:\Program Files\QEMU\qemu-system-x86_64.exe"

if not exist "!QEMU!" (
    echo Error: QEMU not found at "!QEMU!"
    echo Please edit this script with the correct path.
    pause
    exit /b
)

set "ISO=morphic_os.iso"

REM UEFI Discovery
REM 1. Local OVMF (copied from WSL)
set "OVMF=%~dp0OVMF.fd"
if exist "!OVMF!" goto :found_uefi

REM 2. Mono OVMF
set "OVMF=C:\Program Files\QEMU\share\ovmf.fd"
if exist "!OVMF!" goto :found_uefi

REM 2. Mono EDK2
set "OVMF=C:\Program Files\QEMU\share\edk2-x86_64.fd"
if exist "!OVMF!" goto :found_uefi

REM 3. Split Code (Fallback - might fail with -bios)
set "OVMF=C:\Program Files\QEMU\share\edk2-x86_64-code.fd"
if exist "!OVMF!" (
    echo Warning: Using split code firmware. Boot might fail.
    goto :found_uefi
)

echo Warning: No UEFI Firmware found.
set "BIOS_FLAG="
goto :run_qemu

:found_uefi
echo Using UEFI Firmware: "!OVMF!"
set "BIOS_FLAG=-bios "!OVMF!""

:run_qemu
"!QEMU!" ^
    -m 2048M ^
    -cdrom "!ISO!" ^
    -serial stdio ^
    !BIOS_FLAG! ^
    -s ^
    -vga std ^
    -device e1000,netdev=net0 ^
    -netdev user,id=net0 ^
    -name "MorphicOS Debug"

pause
