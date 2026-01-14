@echo off
REM Morphic OS Direct Runner for Windows (QEMU)
REM Usage: run_direct.bat [morphic_os.iso] [additional qemu args]
REM See run_direct_windows.md for common OVMF locations, required packages and
REM troubleshooting steps.

setlocal enabledelayedexpansion

if "%~1"=="" (
  set "ISO=morphic_os.iso"
) else (
  set "ISO=%~1"
  shift
)

REM Common OVMF locations (user may need to adjust)
set "OVMF_CANDIDATES=%ProgramFiles%\qemu\share\ovmf\OVMF.fd;%ProgramFiles%\qemu\share\ovmf\OVMF_CODE.fd;%ProgramFiles(x86)%\qemu\share\ovmf\OVMF.fd;%ProgramFiles(x86)%\qemu\share\ovmf\OVMF_CODE.fd;%LOCALAPPDATA%\Programs\qemu\share\ovmf\OVMF.fd"
set "OVMF="
for %%P in (%OVMF_CANDIDATES%) do (
  if exist "%%~P" if "!OVMF!"=="" set "OVMF=%%~P"
)

echo ========================================
echo Morphic OS - Debug Mode (Windows)
echo ========================================
echo Serial output: ENABLED (COM1 -> stdio)
echo Type 'desktop' to trigger package loader
echo Mouse grab: click QEMU window (Ctrl+Alt to release)
echo To save log: run_direct.bat morphic_os.iso 2^>^&1 ^| tee boot.log
echo ========================================

REM Create debug_disk.img (2GB) if not exists
if not exist debug_disk.img (
  echo Creating 2GB debug_disk.img...
  where qemu-img >nul 2>nul
  if %ERRORLEVEL%==0 (
    qemu-img create -f raw debug_disk.img 2G
  ) else (
    echo qemu-img not found, creating sparse file via PowerShell...
    powershell -Command "Try { $s=[int64](2*1024*1024*1024); $f=Get-Item -LiteralPath 'debug_disk.img' -ErrorAction SilentlyContinue; if ($f -eq $null) { [System.IO.File]::WriteAllBytes('debug_disk.img', (New-Object byte[] 1)) ; $fs=[System.IO.File]::Open('debug_disk.img','Open','Write'); $fs.SetLength($s); $fs.Close(); } } Catch { Exit 1 }"
  )
)

if "%OVMF%"=="" (
  echo WARNING: OVMF not found in common locations. Set OVMF variable inside run_direct.bat to your OVMF.fd path.
) else (
  echo Using OVMF: %OVMF%
)

REM Build QEMU command
set "QEMU_BASE=qemu-system-x86_64"
set "QEMU_ARGS=-m 2048M -vga std -display sdl -machine pc,i8042=on -serial stdio"

if not "%OVMF%"=="" (
  set "QEMU_ARGS=-bios "%OVMF%" %QEMU_ARGS%"
)

set "QEMU_DRIVES=-drive format=raw,file="%ISO%",index=0,media=disk -drive format=raw,file="debug_disk.img",index=1,media=disk"

echo Running QEMU:
echo %QEMU_BASE% %QEMU_ARGS% %QEMU_DRIVES% %*

REM Execute QEMU
%QEMU_BASE% %QEMU_ARGS% %QEMU_DRIVES% %*

endlocal
