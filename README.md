# Morphic OS

Morphic es un **sistema operativo experimental** (hobby OS) escrito desde cero.

**Tipo de sistema:**
- Kernel monolítico *freestanding* en C++ (sin libc/stdlib) con una HAL propia.
- Boot nativo **UEFI** (aplicación EFI `BOOTX64.EFI`).
- Arquitectura objetivo: **x86_64**.
- Userspace mínimo con **syscalls**; el “Desktop” actual corre como app en userspace y se empaqueta en un `.mpk` embebido en el kernel.
## Arquitectura del Sistema - Pila de Software

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              USERSPACE (Ring 3)                                  │
├─────────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐                   │
│  │    Desktop      │  │   Calculator    │  │    Terminal     │   Aplicaciones   │
│  │   (desktop/)    │  │  (calculator/)  │  │   (terminal/)   │      .mpk        │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘                   │
│           │                    │                    │                            │
│  ┌────────┴────────────────────┴────────────────────┴────────┐                   │
│  │                    SDK Userspace                          │                   │
│  │  ┌─────────────┐ ┌──────────────┐ ┌────────────────────┐  │                   │
│  │  │ runtime.cpp │ │ syscalls.asm │ │ morphic_syscalls.h │  │                   │
│  │  │  (C++ RT)   │ │  (SYSCALL)   │ │   (API Headers)    │  │                   │
│  │  └─────────────┘ └──────────────┘ └────────────────────┘  │                   │
│  └───────────────────────────┬───────────────────────────────┘                   │
│                              │                                                   │
│                        SYSCALL ABI                                               │
│               (RAX=num, RDI/RSI/RDX=args)                                        │
╠══════════════════════════════╪══════════════════════════════════════════════════╣
│                              │                                                   │
│                              ▼            KERNEL (Ring 0)                        │
├─────────────────────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────────────────────┐  │
│  │                         MORPHIC API LAYER                                 │  │
│  │                       (kernel/api/morphic_api.h)                          │  │
│  │   Syscall Dispatcher │ Graphics API │ Audio API │ Input API │ Memory API │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                           KERNEL CORE                                     │  │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────────────┐    │  │
│  │  │ kernel_main │ │   Shell     │ │   Loader    │ │    Boot Config    │    │  │
│  │  │   (entry)   │ │  (mcl/)     │ │ (MPK Apps)  │ │   (bootconfig)    │    │  │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └───────────────────┘    │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                       PROCESS MANAGEMENT                                  │  │
│  │  ┌─────────────────────────────────────────────────────────────────────┐  │  │
│  │  │                      Scheduler (scheduler.cpp)                      │  │  │
│  │  │      Task State │ Context Switch │ Sleep │ Event Queue (IPC)        │  │  │
│  │  └─────────────────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                       MEMORY MANAGEMENT (mm/)                             │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │  │
│  │  │     PMM      │  │     Heap     │  │  User Heap   │  │Write Combine │   │  │
│  │  │ (Physical)   │  │   (kmalloc)  │  │  (userspace) │  │   (MMIO)     │   │  │
│  │  │ Bitmap Alloc │  │  Free-List   │  │   mapping    │  │  Framebuf    │   │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘   │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                      VIRTUAL FILE SYSTEM (fs/)                            │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                     │  │
│  │  │     VFS      │  │   InitRD     │  │    Mount     │                     │  │
│  │  │ (vfs.cpp)    │  │ (initrd.cpp) │  │ (mount.cpp)  │                     │  │
│  │  │  Open/Read   │  │ Embedded FS  │  │  Mountpoints │                     │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘                     │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│                     HARDWARE ABSTRACTION LAYER (HAL)                             │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                      Device Registry (hal/device_registry.cpp)            │  │
│  │         Register() │ GetPrimaryInput() │ GetPrimaryVideo() │ Enumerate    │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│          │                    │                    │                    │        │
│  ┌───────┴───────┐    ┌───────┴───────┐    ┌───────┴───────┐    ┌───────┴─────┐ │
│  │  VIDEO (hal/) │    │  INPUT (hal/) │    │  AUDIO (hal/) │    │STORAGE(hal/)│ │
│  ├───────────────┤    ├───────────────┤    ├───────────────┤    ├─────────────┤ │
│  │ video_manager │    │ input_manager │    │  audio.cpp    │    │storage_mgr  │ │
│  │ compositor    │    │   mouse.cpp   │    │  mixer.cpp    │    │buffer_cache │ │
│  │ graphics.cpp  │    │  keymap.cpp   │    │   wav.cpp     │    │ partition   │ │
│  │ font_renderer │    │ input_device  │    │ ring_buffer   │    │block_device │ │
│  │  vsync.cpp    │    └───────────────┘    └───────────────┘    └─────────────┘ │
│  │  blit_fast.S  │                                                              │
│  │  (SIMD copy)  │                                                              │
│  └───────────────┘                                                              │
│                                      │                                           │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│                           DRIVERS (kernel/drivers/)                              │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│  ┌────────────────────────────────── x86_64 ────────────────────────────────┐   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │   │
│  │  │   PIT    │  │ Keyboard │  │   IDE    │  │ UART8250 │  │  Mouse   │   │   │
│  │  │ (Timer)  │  │  PS/2    │  │  (ATA)   │  │ (Serial) │  │  PS/2    │   │   │
│  │  │ 1000 Hz  │  │  IRQ1    │  │ Storage  │  │  Debug   │  │  IRQ12   │   │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │   │
│  └──────────────────────────────────────────────────────────────────────────┘   │
│                                      │                                           │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│                          ARCH LAYER (kernel/arch/)                               │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                          Platform (HAL/Platform)                          │  │
│  │     GDT │ IDT │ TSS │ Interrupts │ MMU │ Exceptions │ Spinlock            │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
╠══════════════════════════════════════╪══════════════════════════════════════════╣
│                               BOOTLOADER                                         │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                    UEFI Application (boot/src/main.cpp)                   │  │
│  │   Load ELF Kernel │ Setup Framebuffer │ Build BootInfo │ Exit Boot Svcs   │  │
│  │                        ↓ Outputs: BOOTX64.EFI                             │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
╠══════════════════════════════════════╪══════════════════════════════════════════╣
│                               FIRMWARE                                           │
├──────────────────────────────────────┼──────────────────────────────────────────┤
│  ┌───────────────────────────────────┴───────────────────────────────────────┐  │
│  │                              UEFI Firmware                                │  │
│  │     Boot Services │ Runtime Services │ GOP (Graphics) │ Memory Map        │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│                                      │                                           │
╠══════════════════════════════════════╪══════════════════════════════════════════╣
│                                HARDWARE                                          │
├──────────────────────────────────────┴──────────────────────────────────────────┤
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────┐  │
│  │    CPU      │ │    RAM      │ │ Framebuffer │ │   i8042     │ │    RTC    │  │
│  │   x86_64    │ │   Memory    │ │   (VGA)     │ │  PS/2 Ctrl  │ │   CMOS    │  │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘ └───────────┘  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Descripción de las Capas

| Capa | Descripción |
|------|-------------|
| **Userspace** | Aplicaciones en Ring 3 empaquetadas como `.mpk`. Usan syscalls para comunicarse con el kernel. |
| **SDK** | Runtime C++, stubs de syscalls en ASM, y headers compartidos para desarrollo de apps. |
| **Morphic API** | Capa de dispatch de syscalls. Provee APIs de alto nivel para gráficos, audio, input y memoria. |
| **Kernel Core** | Punto de entrada, shell integrada (MCL), cargador de MPK y configuración de boot. |
| **Process Mgmt** | Scheduler cooperativo/preemptivo con colas de eventos para IPC entre tareas. |
| **Memory Mgmt** | PMM (Physical Memory Manager) con bitmap, heap kernel, heap userspace y write-combining para MMIO. |
| **VFS** | Sistema de archivos virtual con InitRD embebido y soporte de mountpoints. |
| **HAL** | Hardware Abstraction Layer con subsistemas de video, input, audio y storage. |
| **Drivers** | Drivers específicos x86_64: PIT (timer), teclado/mouse PS/2, IDE, UART serial. |
| **Arch Layer** | Código específico de arquitectura: GDT, IDT, TSS, MMU, manejo de excepciones. |
| **Bootloader** | Aplicación UEFI que carga el kernel ELF, configura framebuffer y pasa BootInfo. |
| **Firmware** | UEFI provee servicios de boot, GOP para gráficos y mapa de memoria. |
| **Hardware** | CPU x86_64, RAM, framebuffer VGA, controlador PS/2, RTC/CMOS. |

### Flujo de Datos Principal

```
┌──────────────┐     syscall      ┌──────────────┐     HAL API      ┌──────────────┐
│   Userspace  │ ───────────────► │    Kernel    │ ───────────────► │   Drivers    │
│     App      │                  │   Core/API   │                  │   Hardware   │
└──────────────┘ ◄─────────────── └──────────────┘ ◄─────────────── └──────────────┘
                    return/IRQ                        IRQ/data
```
## Características (estado actual)

- **UEFI Boot**: arranque por UEFI (sin BIOS legacy).
- **Interrupciones + drivers básicos**: PIT, teclado, mouse PS/2 (i8042 AUX / IRQ12).
- **Render por software**: backbuffer en RAM + present al framebuffer.
- **Optimización real de gráficos**:
	- Copia SIMD (`blit_fast_32`) para presentar.
	- **VSync best-effort** (polling del puerto VGA `0x3DA`) con timeout para no colgarse.
	- **Dirty-rect present**: syscall para copiar solo la región “sucia” (reduce stutter y carga de CPU).
- **Desktop minimalista (userspace)**:
	- Taskbar con iconos de ventanas.
	- Menú simple.
	- Botones de ventana: minimizar / maximizar / “cerrar” (en esta versión, cerrar equivale a minimizar).
	- Reloj simple (por ahora basado en uptime; no RTC real todavía).

## Quick Start

### Requisitos

```bash
./setup.sh
```

### Build

```bash
make
```

Esto genera:
- `morphic.img` (imagen FAT con `EFI/BOOT/BOOTX64.EFI`, `morph_kernel.elf` y `desktop.mpk`).

### Run (recomendado para input/depurar)

```bash
./run_direct.sh 2>&1 | tee boot.log
```

Notas:
- El mouse en QEMU requiere “grab”: click en la ventana, `Ctrl+Alt` para soltar.
- Para lanzar el desktop, en la shell escribe `desktop`.

### Run por noVNC (web)

```bash
./run_web.sh
```

Luego abre: `http://localhost:8080/vnc.html`

## Flags útiles

- `MOUSE_DEBUG=1` activa trazas extra del mouse/IRQ/timing:

```bash
make clean && make MOUSE_DEBUG=1
```

## Syscalls (ABI userspace)

Morphic usa `SYSCALL` en x86_64.

- `RAX` = número de syscall
- `RDI` = arg1, `RSI` = arg2, `RDX` = arg3
- retorno en `RAX`

Structs compartidos (kernel ↔ userspace):
- `OSEvent`: ver `shared/os_event.h`
- `MorphicDateTime` y `MorphicSystemInfo`: ver `shared/system_info.h`

### Lista de syscalls (principales)

- `0  SYS_EXIT` → (no usado en userspace actualmente)
- `1  SYS_WRITE(arg1=ptr, arg2=?, arg3=len)` → escribe a consola (debug)
- `3  SYS_MALLOC(arg1=size)` → puntero
- `4  SYS_FREE(arg1=ptr)`
- `10 SYS_UPDATE_SCREEN` → compone/flip (GUI kernel)
- `11 SYS_GET_SCREEN_INFO` → `((width << 32) | height)`
- `12 SYS_BEEP(arg1=freq_hz, arg2=duration_ms)`
- `13 SYS_SLEEP(arg1=ms)`
- `20 SYS_GET_TIME_MS` → ms desde boot (PIT a 1000Hz)
- `21 SYS_GET_EVENT(arg1=OSEvent*)` → `1` si entregó evento, `0` si no
- `50 SYS_VIDEO_MAP` → mapea framebuffer (MMIO) a userspace, retorna puntero
- `51 SYS_VIDEO_FLIP(arg1=backbuffer_ptr)` → presenta backbuffer completo, retorna `1` si VSync (best-effort)
- `53 SYS_ALLOC_BACKBUFFER(arg1=size_bytes)` → retorna puntero al backbuffer en userspace (RAM cacheable)
- `54 SYS_VIDEO_FLIP_RECT(arg1=backbuffer_ptr, arg2=(x<<32)|y, arg3=(w<<32)|h)` → presenta solo rectángulo (dirty-rect)
- `55 SYS_GET_RTC_DATETIME(arg1=MorphicDateTime*)` → `1` si OK (RTC/CMOS)
- `56 SYS_GET_SYSTEM_INFO(arg1=MorphicSystemInfo*)` → `1` si OK

## Rendimiento: qué se optimiza y por qué

- **Backbuffer cacheable**: el backbuffer de userspace se mapea como RAM normal (cacheable). Mapearlo `NOCACHE` provoca stutter severo.
- **Present parcial (dirty rect)**: el desktop calcula un rectángulo sucio y presenta solo esa región para minimizar el coste por frame.
- **Timing consistente**: el PIT se configura a **1000Hz** para que `SYS_GET_TIME_MS`/`SYS_SLEEP` sean milisegundos reales.

## Estructura del proyecto

```text
boot/                   Aplicación UEFI (BOOTX64.EFI)
kernel/                 Kernel freestanding
	hal/                  HAL (video/input/arch)
	drivers/              Drivers x86 (PIT/keyboard/uart)
	fs/                   VFS + assets embebidos (desktop_mpk)
	mcl/                  Parser/commands de la shell
userspace/
	apps/desktop/         Desktop (render por software)
	syscalls.asm          Stubs de syscalls
shared/                 Headers compartidos (OSEvent, boot info, etc.)
scripts/                Utilidades (ISO)
docs/                   Docs internas del proyecto
	- mpk_format.md         Especificación básica del formato .mpk (MPK1)
	- mpk_sdk.md            Convenciones/SDK MVP para Apps MPK
```

## Limitaciones conocidas

- El reloj del desktop todavía no lee RTC real (CMOS/UEFI time). Actualmente es “tiempo desde boot”.
- VSync best-effort depende del adaptador VGA (en QEMU funciona con `-vga std`).

## Licencia

MIT License - ver [LICENSE](LICENSE)
