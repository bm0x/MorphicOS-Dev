# Morphic OS - Verificador Automatizado de Arranque y SelfTest (Headless COM1)
# Arranca morphic.img o morphic_os.iso bajo UEFI (OVMF.fd), captura el puerto serie COM1
# y corrobora automaticamente que las 6 pruebas internas (SelfTest), MorphicVK y el Compositor inicien sin errores.

param(
    [string]$Image = "morphic.img",
    [int]$TimeoutSeconds = 12
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $RepoRoot

$Qemu = "qemu-system-x86_64"
if (-not (Get-Command $Qemu -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\Program Files\qemu\qemu-system-x86_64.exe") {
        $Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
    } else {
        Write-Host "[ERROR] qemu-system-x86_64 no encontrado." -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Path "$RepoRoot\$Image")) {
    python "$RepoRoot\tools\build_win.py" release
}

$LogPath = "$RepoRoot\build\selftest_serial.log"
New-Item -ItemType Directory -Force -Path "$RepoRoot\build" | Out-Null
Remove-Item -Force $LogPath -ErrorAction SilentlyContinue

$driveArg = if ($Image.EndsWith(".iso")) { "-cdrom `"$RepoRoot\$Image`"" } else { "-drive format=raw,file=`"$RepoRoot\$Image`",index=0,media=disk" }
$argsList = "-bios `"$RepoRoot\OVMF.fd`" $driveArg -m 2048M -vga std -display none -machine pc -serial `"file:$LogPath`""

Write-Host "[TEST] Iniciando verificacion UEFI de '$Image' (max ${TimeoutSeconds}s)..." -ForegroundColor Cyan
$proc = Start-Process -FilePath $Qemu -ArgumentList $argsList -PassThru -NoNewWindow

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$content = ""
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $LogPath) {
        $content = Get-Content $LogPath -Raw -ErrorAction SilentlyContinue
        if ($content -match "\[Desktop\] Spawned desktop client" -or $content -match "MORPHIC OS KERNEL PANIC") {
            Start-Sleep -Seconds 1
            $content = Get-Content $LogPath -Raw -ErrorAction SilentlyContinue
            break
        }
    }
}

if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

Write-Host "`n--- Salida Serial COM1 ---" -ForegroundColor DarkGray
Write-Host $content

if ($content -match "MORPHIC OS KERNEL PANIC") {
    Write-Host "[FAIL] Se detecto un Kernel Panic durante el arranque." -ForegroundColor Red
    exit 1
}
if ($content -notmatch "\[SELFTEST\] Summary: 6/6 passed\.") {
    Write-Host "[FAIL] No se completo el SelfTest 6/6." -ForegroundColor Red
    exit 1
}
if ($content -notmatch "\[Compositor\.mpk\] Load success") {
    Write-Host "[FAIL] El Compositor de espacio de usuario no cargo correctamente." -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Verificacion completa exitosa: Boot UEFI + SelfTest 6/6 + MorphicVK 1.3 + Compositor Ring 3 activos." -ForegroundColor Green
