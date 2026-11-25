# Morphic OS - Ejecutor en Microsoft Hyper-V Gen 2 (Alternativa #2 a QEMU, Nativa de Windows)
# Requiere ejecutar PowerShell como Administrador con Hyper-V habilitado.

param(
    [string]$VMName = "MorphicOS-HyperV"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $RepoRoot

if (-not (Get-Command New-VM -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] El modulo de Hyper-V no esta activo en este Windows." -ForegroundColor Red
    Write-Host "Alternativa inmediata sin reiniciar Windows: ejecuta .\run_virtualbox.bat -InstallIfMissing" -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path "$RepoRoot\morphic_os.vhd")) {
    python "$RepoRoot\tools\build_win.py" release
}

$existing = Get-VM -Name $VMName -ErrorAction SilentlyContinue
if ($existing) {
    if ($existing.State -ne "Off") { Stop-VM -Name $VMName -TurnOff -Force }
    Remove-VM -Name $VMName -Force
}

Write-Host "[Hyper-V] Creando VM Generacion 2 (UEFI Nativo) '$VMName'..." -ForegroundColor Cyan
New-VM -Name $VMName -MemoryStartupBytes 2048MB -Generation 2 -VHDPath "$RepoRoot\morphic_os.vhd" | Out-Null
Set-VMProcessor -VMName $VMName -Count 2
Set-VMFirmware -VMName $VMName -EnableSecureBoot Off
Set-VMComPort -VMName $VMName -Number 1 -Path "\\.\pipe\morphic_serial"

Start-VM -Name $VMName
Write-Host "[Hyper-V] VM iniciada. Abriendo visor vmconnect (Serial en \\.\pipe\morphic_serial)..." -ForegroundColor Green
Start-Process "vmconnect.exe" -ArgumentList "localhost", $VMName
