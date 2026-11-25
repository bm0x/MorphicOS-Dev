@echo off
REM Morphic OS - One-Click Native Windows Build Script
REM Generates morphic.img (Live USB), morphic_os.iso (VirtualBox/VMware), morphic_os.vhd (Hyper-V), and VMware VMX.

setlocal
cd /d "%~dp0"

echo ========================================================
echo   Morphic OS - Native Build (UEFI + Kernel + MPK + ISO)
echo ========================================================

python tools\build_win.py %*
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

echo ========================================================
echo   [OK] Artifacts ready:
echo     - morphic.img     (Live USB / Raw UEFI FAT32)
echo     - morphic_os.iso  (VirtualBox / VMware UEFI ISO)
echo     - morphic_os.vhd  (Hyper-V Gen 2 / VirtualBox VHD)
echo     - build\vmware\MorphicOS.vmx (VMware Workstation)
echo ========================================================
endlocal
