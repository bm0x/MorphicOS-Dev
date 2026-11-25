# Morphic OS - Instalador y Configurador de Entorno en Windows
# Verifica/instala LLVM, NASM, QEMU y opcionalmente Oracle VirtualBox (alternativa recomendada a QEMU),
# y agrega los directorios binarios al PATH del usuario de forma permanente.

param(
    [switch]$WithVirtualBox
)

$ErrorActionPreference = "Stop"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "  Morphic OS - Setup de Toolchain y Hipervisores (Win)  " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

$packages = @(
    @{ Id = "LLVM.LLVM"; Name = "LLVM / Clang / LLD"; TestPath = "C:\Program Files\LLVM\bin\clang++.exe" },
    @{ Id = "NASM.NASM"; Name = "NASM Assembler"; TestPath = "$env:LOCALAPPDATA\bin\NASM\nasm.exe" },
    @{ Id = "SoftwareFreedomConservancy.QEMU"; Name = "QEMU x86_64"; TestPath = "C:\Program Files\qemu\qemu-system-x86_64.exe" }
)

if ($WithVirtualBox) {
    $packages += @{ Id = "Oracle.VirtualBox"; Name = "Oracle VirtualBox (UEFI VM)"; TestPath = "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" }
}

foreach ($pkg in $packages) {
    $cmdName = [System.IO.Path]::GetFileNameWithoutExtension($pkg.TestPath)
    $exists = (Test-Path $pkg.TestPath) -or ($null -ne (Get-Command $cmdName -ErrorAction SilentlyContinue))
    if ($exists) {
        Write-Host "[OK] $($pkg.Name) ya instalado." -ForegroundColor Green
    } else {
        Write-Host "[INSTALL] Instalando $($pkg.Name) ($($pkg.Id))..." -ForegroundColor Yellow
        winget install -e --id $pkg.Id --accept-package-agreements --accept-source-agreements
    }
}

$binDirs = @(
    "C:\Program Files\LLVM\bin",
    "$env:LOCALAPPDATA\bin\NASM",
    "C:\Program Files\NASM",
    "C:\Program Files\qemu",
    "C:\Program Files\Oracle\VirtualBox"
)

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$updated = $false
foreach ($dir in $binDirs) {
    if ((Test-Path $dir) -and ($userPath -notlike "*$dir*")) {
        $userPath = "$userPath;$dir"
        $updated = $true
        Write-Host "[PATH] Agregado al PATH de usuario: $dir" -ForegroundColor Cyan
    }
    if ((Test-Path $dir) -and ($env:Path -notlike "*$dir*")) {
        $env:Path = "$env:Path;$dir"
    }
}

if ($updated) {
    [Environment]::SetEnvironmentVariable("Path", $userPath.TrimStart(';'), "User")
}

Write-Host "`n[LISTO] Entorno configurado. Ejecuta .\build_windows.bat para compilar Morphic OS." -ForegroundColor Green
