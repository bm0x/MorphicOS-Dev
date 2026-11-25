# Morphic OS - Grabador Seguro de Live USB en Windows (Modo DD Raw)
# Requiere PowerShell como Administrador. Bloquea estrictamente discos internos SATA/NVMe.

param(
    [int]$DiskNumber = -1
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$ImgPath = "$RepoRoot\morphic.img"

if (-not (Test-Path $ImgPath)) {
    Write-Host "[USB] Compilando morphic.img primero..." -ForegroundColor Cyan
    python "$RepoRoot\tools\build_win.py" release
}

$usbDisks = Get-Disk | Where-Object { $_.BusType -eq "USB" -and -not $_.IsSystem -and -not $_.IsBoot }
if (-not $usbDisks) {
    Write-Host "[USB] No se detectaron pendrives USB externos conectados." -ForegroundColor Yellow
    Write-Host "Tambien puedes grabar '$ImgPath' con Rufus seleccionando modo DD." -ForegroundColor Cyan
    exit 0
}

Write-Host "Unidades USB externas detectadas:" -ForegroundColor Cyan
$usbDisks | Format-Table Number, FriendlyName, @{Name="Size(GB)";Expression={[math]::Round($_.Size/1GB,2)}}, PartitionStyle

if ($DiskNumber -lt 0) {
    Write-Host "Para grabar en una unidad especifica (ejecutando como Administrador):" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\flash_live_usb.ps1 -DiskNumber <N>" -ForegroundColor Cyan
    exit 0
}

$target = $usbDisks | Where-Object { $_.Number -eq $DiskNumber }
if (-not $target) {
    Write-Host "[SEGURIDAD] El disco $DiskNumber NO es un pendrive USB externo valido. Abortando." -ForegroundColor Red
    exit 1
}

$confirm = Read-Host "ADVERTENCIA: Se borrara por completo '$($target.FriendlyName)' (Disco $DiskNumber). Escribe 'MORPHIC' para confirmar"
if ($confirm -ne "MORPHIC") {
    Write-Host "Operacion cancelada."
    exit 0
}

Clear-Disk -Number $DiskNumber -RemoveData -RemoveOEM -Confirm:$false
Set-Disk -Number $DiskNumber -IsOffline $true

$rawPath = "\\.\PhysicalDrive$DiskNumber"
$bytes = [System.IO.File]::ReadAllBytes($ImgPath)
$fs = New-Object System.IO.FileStream($rawPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
try {
    $fs.Write($bytes, 0, $bytes.Length)
    $fs.Flush()
} finally {
    $fs.Close()
    Set-Disk -Number $DiskNumber -IsOffline $false
}

Write-Host "[OK] Live USB de Morphic OS grabado correctamente en Disco $DiskNumber ($($target.FriendlyName))." -ForegroundColor Green
