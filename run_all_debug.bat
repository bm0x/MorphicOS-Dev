@echo off
echo ========================================
echo Morphic OS - Build - Debug Cycle
echo ========================================

echo [1/3] Cleaning...
wsl make clean
if %ERRORLEVEL% NEQ 0 (
    echo Error: Make clean failed.
    pause
    exit /b
)

echo [2/3] Building (WSL)...
wsl make iso
if %ERRORLEVEL% NEQ 0 (
    echo Error: Build failed.
    pause
    exit /b
)

echo [3/3] Launching Debugger...
if not exist "morphic_os.iso" (
    echo Warning: proper ISO path not found/verified in batch.
)

call run_debug.bat
