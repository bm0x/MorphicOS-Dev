# Informe Técnico de Investigación: Diagnóstico del Desktop y Pipeline Gráfico

**Morphic OS — Documento de Análisis de Arquitectura y Fallos de Renderizado**  
**Fecha:** 12 de Septiembre de 2026  
**Rama:** `main`  
**Referencia Arquitectónica:** [DEVELOPMENT_ARCHITECTURE_SPEC.md](DEVELOPMENT_ARCHITECTURE_SPEC.md) | [AGENTS.md](../AGENTS.md)

---

## 1. Resumen Ejecutivo del Diagnóstico

Al iniciar Morphic OS bajo UEFI en QEMU (`-bios ./OVMF.fd`), el sistema ejecutaba correctamente la secuencia de inicialización del kernel y el cargador de paquetes ELF (`PackageLoader`) iniciaba `Compositor.mpk` (Desktop). Sin embargo, tras el final de la pantalla de carga (`BootScreen::Finish()`), la pantalla permanecía en negro y el entorno de escritorio no se presentaba visualmente.

### Causa Raíz Primaria (Confirmada y Verificada Empíricamente)
El despachador de la llamada al sistema DRM (`SYS_DRM_ATOMIC_COMMIT` / `SYS_DRM_PRESENT`) sufría un **bloqueo infinito (hang / freeze) en el primer cuadro (Frame 1)** dentro de la función de sincronización vertical de bajo nivel:
```cpp
// kernel/hal/drm/drm.cpp:374
static void WaitVBlankVGA() {
    while (inb(0x3DA) & 0x08) { asm volatile("pause"); }
    while (!(inb(0x3DA) & 0x08)) { asm volatile("pause"); }
}
```

#### ¿Por qué fallaba en UEFI/QEMU?
1. En entornos modernos basados en firmware UEFI (como OVMF EDK2) con adaptadores de video emulados (`-vga std` o bochs-display), el controlador gráfico opera exclusivamente mediante el **GOP (Graphics Output Protocol)** sobre un framebuffer lineal en memoria física (`0x80000000`).
2. Los puertos heredados de sincronización CRT de VGA estándar (`0x3DA` - *Input Status Register 1*) **no oscilan ni generan el ciclo de retrazo vertical (`bit 3`)** porque el secuenciador y reloj CRT de VGA clásica (modo 13h/3h) nunca fueron inicializados por una BIOS Legacy (int 10h).
3. La función `WaitVBlankVGA()` carecía de un contador de tiempo límite (*timeout*). Al evaluar `while (!(inb(0x3DA) & 0x08))`, quedaba atrapada en un bucle ciego infinito en Ring 0.
4. Como `WaitVBlank()` se ejecuta **antes** de la transferencia a memoria de video (`blit_fast_32(vram_buffer, compositor_buffer, ...)`), la copia de los píxeles a la VRAM física **nunca llegaba a ejecutarse**. El Frame 1 se congelaba y la pantalla quedaba permanentemente negra.

---

## 2. Validación y Verificación Empírica

Se introdujo un límite de espera acotado (*bounded timeout*) de ~1.5 ms en `WaitVBlankVGA()` idéntico a la disciplina aplicada en `BGADriver::WaitVSync()`:

```cpp
static void WaitVBlankVGA() {
    uint32_t timeout = 2000;
    while ((inb(0x3DA) & 0x08) && --timeout) {
        asm volatile("pause");
    }
    timeout = 50000;
    while (!(inb(0x3DA) & 0x08) && --timeout) {
        asm volatile("pause");
    }
}
```

### Salida del Registro de Ejecución COM1 (Serial) tras la corrección:
```text
[Compositor.mpk] Load success. Entry: 0x600000000000 Stack: 0x6FFFFFFF0000 CR3: 0x20000000
[userspace] early: start
[Desktop] main: before Compositor::Initialize
[Syscall] SYS_GET_SCREEN_INFO -> 0x78000000438
[Compositor] frontBuffer=0x0000600100000000
[Compositor] backend_gpu_experimental enabled
[Compositor] backBuffer=0x0000600200000000
[Compositor] Width=1920 Height=1080
[Desktop] Compositor::Initialize OK
[Desktop] Render backend: backend_gpu_experimental
[Desktop] Registered as Compositor
[Desktop] Using DRM atomic commit
[Desktop] Spawned desktop client
[SYS_CREATE_WINDOW] Entry - caller task: 3, w=520, h=220
[Syscall] Window Created ID: 1
[ComposeAppWindowsOnly] Found APP_WINDOW: visible=1 buffer=0x255C3000 pos=700,430 size=520x220 px0=0xFF20344A
[Desktop][Sync] SCOM=0 SACK=0 STO=0 SOVR=0 SURF=0
[Desktop] Client HELLO PID=3 SIZE=520x220
[Desktop] Client CREATE_SURFACE PID=3 SIZE=520x220
[Desktop] Surface bound PID=3 SIZE=520x220
[Desktop][Sync] SCOM=11 SACK=11 STO=0 SOVR=0 SURF=1
```
**Resultado:** El bucle de eventos del Compositor y las aplicaciones cliente cobran vida inmediatamente, completando la sincronización de superficies de protocolo y confirmación de cuadros (`SCOM=11`, `SACK=11`, `SURF=1`).

---

## 3. Vulnerabilidades y Defectos Secundarios Identificados

Durante la auditoría del flujo de renderizado se localizaron los siguientes puntos críticos que deben ser atendidos para garantizar robustez en cualquier máquina anfitrión:

### 3.1. Inconsistencia de `drawBuffer` en `kernel/hal/video/graphics.cpp` durante Boot
- **Problema:** Al iniciar el sistema en `Graphics::Init()`, `drawBuffer` se apunta a `vramBuffer`. Luego se inicializa DRM (`Graphics::InitDRM()`), el cual asigna un búfer intermedio (`compositor_buffer`) con fondo gris `0xFF181818`.
- **Efecto:** Todas las llamadas de `BootScreen::Update()` dibujan en `vramBuffer`, pero cuando ejecutan `Graphics::FlipRect()`, `DRM::Present()` copia desde `compositor_buffer` hacia `vram_buffer`, sobrescribiendo la barra de carga con el fondo gris oscuro o negro.
- **Acción requerida:** En `Graphics::InitDRM()`, establecer inmediatamente `Graphics::SetDrawBuffer(compositor_buffer)` para que todo dibujado del kernel opere sobre el backbuffer antes de flipear.

### 3.2. Falta de Soporte para Huge Pages (`PTE_HUGE`) en `MMU::GetPhysical`
- **Problema:** En `kernel/hal/arch/x86_64/mmu.cpp`, `MMU::GetPhysical(virt)` asume que todos los niveles de paginación conducen a una tabla de páginas de 4KB (`pt`), sin comprobar el bit 7 (`PTE_HUGE` / `0x80`) en `PDPT` (páginas de 1GB) o `PD` (páginas de 2MB).
- **Riesgo:** Si una dirección virtual (como el búfer del compositor o memoria del heap) cae en una región identity-mapped por UEFI mediante páginas de 2MB, `GetPhysical` interpreta los datos almacenados en esa RAM física como si fueran una tabla de páginas subordinada, produciendo direcciones físicas corruptas o retornando 0.
- **Acción requerida:** Añadir la comprobación de `PTE_HUGE` en `mmu.cpp`:
  ```cpp
  if (pd[PD_INDEX(virt)] & PTE_HUGE) {
      return (pd[PD_INDEX(virt)] & 0x000FFFFFFFEE00000ULL) | (virt & 0x1FFFFF);
  }
  ```

### 3.3. Asignación de `compositor_buffer` desde `KHeap` vs `PMM::AllocContiguous`
- **Problema:** El búfer de pantalla de 1920x1080 (8.3 MB) se reserva en `DRM::Init()` llamando a `KHeap::Allocate(buffer_size)`.
- **Incompatibilidad con `AGENTS.md` (Regla 2.1):** `KHeap` utiliza cabeceras monolíticas con magic `0xC0FFEE` pensadas para objetos pequeños del kernel. Un búfer de video compartido debe ser obtenido directamente de `PMM::AllocContiguous(pages)` para garantizar contigüidad física verificable y permitir que `SYS_VIDEO_MAP` lo exponga con banderas `PAGE_USER | PAGE_WRITABLE` sin depender de resolver la fragmentación del heap.

### 3.4. Ausencia de Barrera de Escritura (`sfence`) en VRAM
- **Problema:** Morphic OS activa Write-Combining mediante PAT (`WriteCombining::InitPAT()`).
- **Efecto:** Cuando la CPU escribe millones de píxeles hacia la VRAM en `blit_fast_32`, los búferes de escritura combinada (*store buffers*) pueden quedar retenidos temporalmente sin vaciarse al bus PCIe/VGA.
- **Acción requerida:** Ejecutar `__builtin_ia32_sfence()` o `asm volatile("sfence" ::: "memory")` inmediatamente tras la llamada a `blit_fast_32` en `DRM::Present()`.

---

## 4. Guía de Ejecución en Nuevos Equipos

Para continuar las pruebas en otra máquina de desarrollo:

1. **Clonar / Descargar los cambios:**
   ```bash
   git pull origin main
   ```
2. **Compilar y generar imagen ISO:**
   ```bash
   make clean
   make -j$(nproc)
   make iso
   ```
3. **Ejecutar en QEMU:**
   - En Linux:
     ```bash
     ./run_direct.sh
     ```
   - En Windows:
     ```cmd
     run_direct.bat
     ```
   - Si no se cuenta con servidor X11/Wayland nativo o se ejecuta por terminal SSH, utilizar `-display none -serial stdio` para monitorear los registros del puerto serial en vivo:
     ```bash
     qemu-system-x86_64 -bios ./OVMF.fd -cdrom morphic_os.iso -m 2048M -smp 4 -vga std -display none -serial stdio
     ```
