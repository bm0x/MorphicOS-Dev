# MCL - Morphic Command Language Reference

## Overview

MCL es el sistema de comandos nativo de Morphic OS. A diferencia de Bash o CMD, MCL usa una sintaxis de **Lenguaje Natural Estructurado**.

---

## Gramática

```
[Acción] [Objetivo] [Modificador:Valor]
```

| Componente | Descripción | Ejemplo |
|------------|-------------|---------|
| **Acción** | Verbo que indica qué hacer | `list`, `show`, `set`, `create`, `open` |
| **Objetivo** | Recurso sobre el que actuar | `files`, `folders`, `cpu`, `path` |
| **Modificador** | Parámetro con valor | `layout:es`, `file:readme.txt`, `folder:docs` |

---

## Comandos de Navegación

### `show path`
**Descripción:** Muestra el directorio actual.
```
> show path
Current path: /sys
```

### `open folder:nombre`
**Descripción:** Entra a una carpeta (cambia el directorio actual).
```
> open folder:sys
[OK] Current path: /sys

> open folder:/        # Ruta absoluta
[OK] Current path: /
```

### `go back`
**Descripción:** Vuelve al directorio padre.
```
> go back
[OK] Current path: /
```

---

## Comandos de Almacenamiento

### `list files`
**Descripción:** Lista solo los archivos del directorio actual.
```
> list files
Files in /sys:
  info.txt  [42 bytes]
  config    [128 bytes]
```

### `list folders`
**Descripción:** Lista solo las carpetas del directorio actual.
```
> list folders
Folders in /:
  EFI/
  sys/
```

### `list`
**Descripción:** Lista todo el contenido (archivos y carpetas).
```
> list
Contents of /:
  EFI/
  sys/
  boot.cfg  [256 bytes]
```

### `read file:nombre`
**Descripción:** Muestra el contenido de un archivo.
```
> read file:boot.cfg
KEYMAP=ES
VERBOSE=1
```

### `create file:nombre`
**Descripción:** Crea un archivo vacío.
```
> create file:notas.txt
[OK] File created: notas.txt
```

### `delete file:nombre`
**Descripción:** Elimina un archivo.
```
> delete file:notas.txt
[OK] File deleted: notas.txt
```

### `create folder:nombre`
**Descripción:** Crea un directorio.
```
> create folder:datos
[OK] Directory created: datos
```

### `delete folder:nombre`
**Descripción:** Elimina un directorio (debe estar vacío).
```
> delete folder:datos
[OK] Directory deleted: datos
```

---

## Comandos de Hardware

### `show cpu`
**Descripción:** Muestra información del procesador.
```
> show cpu
Architecture: x86_64
Vendor: Generic
```

### `show memory`
**Descripción:** Muestra estado de la memoria RAM.
```
> show memory
Heap Base: 0x400000
Heap Size: 16 MB
```

### `show version`
**Descripción:** Muestra la versión del sistema.
```
> show version
Morphic OS v0.5 - Phase Swift HAL
MCL Engine Active
```

### `check memory`
**Descripción:** Ejecuta prueba de integridad de memoria.
```
> check memory
Running memory test...
[OK] Memory integrity verified
```

### `test audio`
**Descripción:** Reproduce un tono de prueba (440 Hz).
```
> test audio
Playing test tone (440 Hz)...
```

### `scan bus:pci`
**Descripción:** Enumera dispositivos PCI.
```
> scan bus:pci
PCI Bus Enumeration:
  00:00.0 Host Bridge
  00:01.0 VGA Controller (Cirrus/QEMU)
```

---

## Comandos de Sistema

### `set layout:código`
**Descripción:** Cambia la distribución del teclado.
**Valores:** `us`, `es`, `la`
```
> set layout:es
[Keymap] Switched to: ES
```

### `set volume:nivel`
**Descripción:** Ajusta el volumen de audio (0-100).
```
> set volume:80
[OK] Volume adjusted
```

### `toggle verbose`
**Descripción:** Activa/desactiva el modo de depuración.
```
> toggle verbose
[Verbose] Debug output toggled
```

### `reboot now`
**Descripción:** Reinicia el sistema inmediatamente.
```
> reboot now
Rebooting...
```

### `reboot safe`
**Descripción:** Reinicia sincronizando datos primero.
```
> reboot safe
Syncing filesystems...
Rebooting...
```

### `shutdown now`
**Descripción:** Apaga el sistema.
```
> shutdown now
Shutting down...
System halted. You may power off now.
```

---

## Autocompletado

Al presionar **TAB**, MCL sugiere el siguiente elemento válido:

| Entrada | Sugerencias |
|---------|-------------|
| `list ` | files, folders |
| `show ` | cpu, memory, version, path |
| `set `  | layout, volume |
| `create ` | file:, folder: |
| `delete ` | file:, folder: |
| `open ` | folder: |
| `go ` | back |
| `scan bus:` | pci |

---

## Códigos de Retorno

| Código | Significado |
|--------|-------------|
| `[OK]` | Comando exitoso |
| `[ERROR]` | Error en ejecución |
| `[UNKNOWN]` | Comando no reconocido |
| `[MISSING]` | Falta parámetro requerido |
| `[INFO]` | Mensaje informativo |

---

## Ejemplos de Flujo de Trabajo

```bash
# Explorar el sistema de archivos
> show path
Current path: /

> list folders
Folders in /:
  EFI/
  sys/

> open folder:sys
[OK] Current path: /sys

> list files
Files in /sys:
  info.txt  [42 bytes]

> read file:info.txt
Morphic OS - System Information

> go back
[OK] Current path: /

# Crear y eliminar archivos
> create file:test.txt
[OK] File created: test.txt

> list files
Files in /:
  test.txt  [0 bytes]

> delete file:test.txt
[OK] File deleted: test.txt

# Configurar sistema
> set layout:es
[Keymap] Switched to: ES

> show version
Morphic OS v0.5 - Phase Swift HAL
MCL Engine Active
```
