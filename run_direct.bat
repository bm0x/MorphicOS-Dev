@echo off
REM Morphic OS Direct Runner for Windows (QEMU, UEFI/OVMF)
REM Usage: run_direct.bat [morphic_os.iso|morphic.img] [extra qemu args]
REM Logs (always written, survive panics / triple faults):
REM   build\serial.log    COM1 serial output
REM   build\debugcon.log  port 0xE9 mirror of the kernel log
REM   build\qemu_cpu.log  QEMU CPU resets / guest errors (triple fault details)

setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist build mkdir build

set "IMG="
if not "%~1"=="" (
  set "IMG=%~1"
  shift
) else if exist "morphic_os.iso" (
  set "IMG=morphic_os.iso"
) else if exist "morphic.img" (
  set "IMG=morphic.img"
)
if "!IMG!"=="" (
  echo [ERROR] No morphic_os.iso / morphic.img found. Run build_windows.bat first.
  exit /b 1
)

REM ---- QEMU ----
set "QEMU=qemu-system-x86_64"
where qemu-system-x86_64 >nul 2>nul
if errorlevel 1 (
  if exist "%ProgramFiles%\qemu\qemu-system-x86_64.exe" (
    set "QEMU=%ProgramFiles%\qemu\qemu-system-x86_64.exe"
  ) else (
    echo [ERROR] qemu-system-x86_64 not found. Run: powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
    exit /b 1
  )
)

REM ---- OVMF (UEFI firmware) ----
set "OVMF="
if exist "OVMF.fd" set "OVMF=%CD%\OVMF.fd"
if "!OVMF!"=="" if exist "%ProgramFiles%\qemu\share\edk2-x86_64-code.fd" set "OVMF=%ProgramFiles%\qemu\share\edk2-x86_64-code.fd"
if "!OVMF!"=="" if exist "%ProgramFiles%\qemu\share\ovmf\OVMF.fd" set "OVMF=%ProgramFiles%\qemu\share\ovmf\OVMF.fd"
if "!OVMF!"=="" (
  echo [ERROR] OVMF.fd not found. Put OVMF.fd in the repo root.
  exit /b 1
)

REM ---- Drive ----
if /I "!IMG:~-4!"==".iso" (
  set "DRIVE=-cdrom "!IMG!""
) else (
  set "DRIVE=-drive format=raw,file="!IMG!",index=0,media=disk"
)

REM ---- Display backend: gtk if available, else sdl ----
set "DISP=sdl"
"!QEMU!" -display help 2>&1 | findstr /I /C:"gtk" >nul && set "DISP=gtk"

echo ========================================
echo Morphic OS - QEMU UEFI (Windows)
echo Image   : !IMG!
echo Firmware: !OVMF!
echo Display : !DISP!
echo Logs    : build\serial.log  build\debugcon.log  build\qemu_cpu.log
echo ========================================

REM -serial mon:stdio would hide logs on crash; use file + debugcon so nothing is lost.
"!QEMU!" -bios "!OVMF!" !DRIVE! -m 2048M -smp 2 -vga std -display !DISP! -machine pc ^
  -serial file:build\serial.log ^
  -debugcon file:build\debugcon.log -global isa-debugcon.iobase=0xe9 ^
  -d guest_errors,cpu_reset -D build\qemu_cpu.log -no-reboot -no-shutdown %1 %2 %3 %4 %5 %6 %7 %8 %9

echo.
echo ===== QEMU exited. Last serial lines =====
if exist build\serial.log powershell -NoProfile -Command "Get-Content build\serial.log -Tail 40"
endlocal
