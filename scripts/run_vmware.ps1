# Morphic OS - Ejecutor en VMware Workstation / Player (Alternativa #3 a QEMU)
# Usa el descriptor build\vmware\MorphicOS.vmx generado automaticamente por tools\build_win.py.

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $RepoRoot

$VmxPath = "$RepoRoot\build\vmware\MorphicOS.vmx"
if (-not (Test-Path $VmxPath)) {
    python "$RepoRoot\tools\build_win.py" release
}

$VmwareBins = @(
    "C:\Program Files (x86)\VMware\VMware Workstation\vmware.exe",
    "C:\Program Files (x86)\VMware\VMware Player\vmplayer.exe",
    "C:\Program Files\VMware\VMware Workstation\vmware.exe"
)

foreach ($bin in $VmwareBins) {
    if (Test-Path $bin) {
        Write-Host "[VMware] Lanzando MorphicOS.vmx con $bin..." -ForegroundColor Green
        Start-Process $bin -ArgumentList "`"$VmxPath`""
        exit 0
    }
}

Write-Host "[INFO] Archivo VMware listo en: $VmxPath" -ForegroundColor Cyan
Write-Host "Abre MorphicOS.vmx directamente desde VMware Workstation Player/Pro." -ForegroundColor Yellow
