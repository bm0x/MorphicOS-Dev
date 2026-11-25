# Morphic OS - Ejecutor Automatizado en Oracle VirtualBox (Alternativa #1 a QEMU)
# Crea o actualiza la VM UEFI "MorphicOS-UEFI" con aceleracion de hardware (VT-x/AMD-V),
# video VMSVGA 128 MB, mouse/teclado PS/2 nativos y salida serial COM1 hacia boot_vbox.log.

param(
    [switch]$Headless,
    [switch]$InstallIfMissing
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $RepoRoot

$VBoxCandidates = @(
    "C:\Program Files\Oracle\VirtualBox\VBoxManage.exe",
    "C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe"
)
$VBoxManage = $null
$cmd = Get-Command VBoxManage -ErrorAction SilentlyContinue
if ($cmd) { $VBoxManage = $cmd.Source }
foreach ($c in $VBoxCandidates) {
    if (-not $VBoxManage -and (Test-Path $c)) { $VBoxManage = $c }
}

if (-not $VBoxManage) {
    if ($InstallIfMissing) {
        Write-Host "[VirtualBox] Instalando Oracle VirtualBox via winget..." -ForegroundColor Yellow
        winget install -e --id Oracle.VirtualBox --accept-package-agreements --accept-source-agreements
        foreach ($c in $VBoxCandidates) {
            if (Test-Path $c) { $VBoxManage = $c; break }
        }
    }
    if (-not $VBoxManage) {
        Write-Host "[ERROR] Oracle VirtualBox no esta instalado." -ForegroundColor Red
        Write-Host "Instalalo en 1 comando con:" -ForegroundColor Yellow
        Write-Host "  .\run_virtualbox.bat -InstallIfMissing" -ForegroundColor Cyan
        exit 1
    }
}

function Invoke-VBox {
    param(
        [Parameter(Mandatory = $true)][string[]]$ArgsList,
        [switch]$IgnoreError
    )
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:VBoxManage @ArgsList 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }
    $text = ($output | Out-String)
    if (-not $IgnoreError -and $code -ne 0) {
        throw "VBoxManage $($ArgsList -join ' ') fallo (exit $code): $($text.Trim())"
    }
    return $text
}

if (-not (Test-Path "$RepoRoot\morphic.img")) {
    Write-Host "[VirtualBox] Compilando Morphic OS primero..." -ForegroundColor Cyan
    python "$RepoRoot\tools\build_win.py" release
    if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion de Morphic OS." }
}

$VMName = "MorphicOS-UEFI"
$VdiPath = "$RepoRoot\build\morphic_vbox.vdi"
$SerialLog = "$RepoRoot\boot_vbox.log"
$EscapedVM = "`"$([regex]::Escape($VMName))`""

# Detener VM previa si estaba corriendo
$runningText = Invoke-VBox -ArgsList @("list", "runningvms") -IgnoreError
if ($runningText -match $EscapedVM) {
    Write-Host "[VirtualBox] Deteniendo instancia activa de '$VMName'..." -ForegroundColor Yellow
    Invoke-VBox -ArgsList @("controlvm", $VMName, "poweroff") -IgnoreError | Out-Null
    Start-Sleep -Seconds 2
}

# Crear VM si no existe (evitando filtro de arrays vacios en PowerShell 5.1)
$vmsText = Invoke-VBox -ArgsList @("list", "vms")
if (-not ($vmsText -match $EscapedVM)) {
    Write-Host "[VirtualBox] Creando VM '$VMName' (64-bit UEFI)..." -ForegroundColor Cyan
    Invoke-VBox -ArgsList @("createvm", "--name", $VMName, "--ostype", "Other_64", "--register") | Out-Null
}
Invoke-VBox -ArgsList @("storagectl", $VMName, "--name", "IDE", "--add", "ide", "--controller", "PIIX4") -IgnoreError | Out-Null

# Desacoplar discos previos antes de regenerar VDI
New-Item -ItemType Directory -Force -Path "$RepoRoot\build" | Out-Null
Invoke-VBox -ArgsList @("storageattach", $VMName, "--storagectl", "IDE", "--port", "0", "--device", "0", "--medium", "none") -IgnoreError | Out-Null
Invoke-VBox -ArgsList @("storageattach", $VMName, "--storagectl", "IDE", "--port", "1", "--device", "0", "--medium", "none") -IgnoreError | Out-Null
Invoke-VBox -ArgsList @("closemedium", "disk", $VdiPath, "--delete") -IgnoreError | Out-Null
if (Test-Path $VdiPath) {
    Remove-Item -Force $VdiPath -ErrorAction SilentlyContinue
}
if (Test-Path $SerialLog) {
    Remove-Item -Force $SerialLog -ErrorAction SilentlyContinue
}

Write-Host "[VirtualBox] Convirtiendo morphic.img -> build\morphic_vbox.vdi..." -ForegroundColor Cyan
Invoke-VBox -ArgsList @("convertfromraw", "$RepoRoot\morphic.img", $VdiPath, "--format", "VDI") | Out-Null

# Configurar UEFI, 2 GB RAM, VMSVGA 128 MB, PS/2 y COM1 -> boot_vbox.log
Invoke-VBox -ArgsList @(
    "modifyvm", $VMName,
    "--firmware", "efi",
    "--memory", "2048",
    "--cpus", "2",
    "--ioapic", "on",
    "--boot1", "dvd",
    "--boot2", "disk",
    "--vram", "128",
    "--graphicscontroller", "vmsvga",
    "--mouse", "ps2",
    "--keyboard", "ps2",
    "--uart1", "0x3F8", "4",
    "--uartmode1", "file", $SerialLog
) | Out-Null

# Inicializar NVRAM UEFI sin Secure Boot en VirtualBox 7+
Invoke-VBox -ArgsList @("modifynvram", $VMName, "inituefivarstore") -IgnoreError | Out-Null

Invoke-VBox -ArgsList @("storageattach", $VMName, "--storagectl", "IDE", "--port", "0", "--device", "0", "--type", "hdd", "--medium", $VdiPath) | Out-Null
if (Test-Path "$RepoRoot\morphic_os.iso") {
    Invoke-VBox -ArgsList @("storageattach", $VMName, "--storagectl", "IDE", "--port", "1", "--device", "0", "--type", "dvddrive", "--medium", "$RepoRoot\morphic_os.iso") | Out-Null
}

$startType = if ($Headless) { "headless" } else { "gui" }
Write-Host "[VirtualBox] Iniciando '$VMName' (modo: $startType) | Log serial: boot_vbox.log" -ForegroundColor Green
Invoke-VBox -ArgsList @("startvm", $VMName, "--type", $startType) | Write-Host
