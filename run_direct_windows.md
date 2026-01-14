# run_direct.bat - Windows guidance

Este archivo complementa `run_direct.bat` y explica rutas comunes de OVMF, dependencias y pasos para ejecutar QEMU en Windows.

Requisitos
- QEMU instalado y en `PATH` (qemu-system-x86_64, qemu-img)
- OVMF (firmware para UEFI) - algunos paquetes incluyen `OVMF.fd` o `OVMF_CODE.fd`
- PowerShell (incluido en Windows 10/11)

Rutas comunes donde `OVMF.fd` puede estar instalado

- Instalación de QEMU para Windows (instalador oficial o scoop/chocolatey):
  - `%ProgramFiles%\qemu\share\ovmf\OVMF.fd`
  - `%ProgramFiles(x86)%\qemu\share\ovmf\OVMF.fd`

- MSYS2/MinGW o paquetes portados:
  - `C:\msys64\usr\share\ovmf\OVMF.fd`

- Instaladores con carpetas de usuario (scoop/choco):
  - `%LOCALAPPDATA%\Programs\qemu\share\ovmf\OVMF.fd`

Si `run_direct.bat` no detecta `OVMF.fd`, puedes editar el archivo y definir manualmente la variable `OVMF` en la parte superior:

```bat
REM Ejemplo: editar y establecer la ruta absoluta
set "OVMF=C:\Program Files\qemu\share\ovmf\OVMF.fd"
```

Instalación recomendada de QEMU en Windows

- Usar Scoop (recomendado):

```powershell
iwr -useb get.scoop.sh | iex
scoop install qemu
```

- O usar Chocolatey:

```powershell
choco install qemu
```

Notas y consejos
- `run_direct.bat` crea un archivo `debug_disk.img` de 2GB si no existe. Si prefieres usar una imagen existente, apunta su nombre al invocar el script.
- Para redirigir logs en PowerShell cmd.exe, usa:

```powershell
.\run_direct.bat morphic_os.iso 2^>^&1 | tee boot.log
```

- Si tu QEMU instalado no incluye `OVMF.fd`, puedes instalar la package de OVMF a través de tu gestor (en WSL: `apt install ovmf`) o copiar `OVMF.fd` desde otra instalación Linux.

Problemas comunes
- QEMU no se encuentra: asegúrate que `qemu-system-x86_64.exe` esté en `PATH`.
- Error al abrir OVMF: apunta `OVMF` a la ruta correcta o instala OVMF.
- Problemas con audio/display: prueba cambiar `-display sdl` por `-display gtk` o `-display none` si tu sistema no soporta SDL GUI.

Si quieres que adapte `run_direct.bat` a un flujo específico (por ejemplo, siempre usar WSL + qemu en Linux), dime y lo ajusto.
