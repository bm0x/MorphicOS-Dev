# Morphic OS — Especificación Maestra de Arquitectura y Desarrollo de Ingeniería

**Versión:** 1.0 (Canónica)  
**Estado:** Norma de Ingeniería Vinculante  
**Destinatarios:** Desarrolladores del Kernel, Arquitectos de Sistema, Diseñadores del SDK y Agentes Autónomos de Codificación (IA).

---

## 1. Misión y Principios Fundamentales del Sistema

Morphic OS es un sistema operativo experimental moderno, monolítico y *freestanding* para la arquitectura **x86_64**, diseñado para arrancar mediante **UEFI nativo**, garantizar separación real de privilegios (**Ring 0 Supervisor vs Ring 3 Usuario**) y proporcionar un entorno gráfico responsivo acelerado por software y extensiones SIMD.

### Los 5 Mandamientos de Ingeniería de Morphic OS
1. **Aislamiento Absoluto de Privilegios:** Ningún puntero interno del espacio de direcciones de Ring 0 (`kmalloc`, estructuras del kernel, registros de control) debe ser expuesto ni compartido directamente a Ring 3 sin un mapeo explícito de usuario (`PAGE_USER`) y validación de límites.
2. **Fuente Única de Verdad (Single Source of Truth):** No se permiten definiciones duplicadas de syscalls, tablas de despacho huérfanas o módulos "maqueta". Lo que está documentado en la ABI debe existir exactamente en el despachador de syscalls.
3. **Determinismo y Cero Residuos:** Toda estructura asignada debe tener un ciclo de vida definido y un mecanismo de destrucción verificado. Jamás se liberarán recursos del asignador de páginas físicas (`PMM`) con funciones del asignador de heap (`kfree`).
4. **Desacoplamiento Estricto de Aplicaciones:** El entorno de escritorio (Desktop/Compositor) es una aplicación independiente de userspace. Las aplicaciones clientes (Calculadora, Terminal, Explorador de Archivos) son procesos aislados empacados en formato `.mpk` que se comunican mediante IPC y protocolos de superficie, jamás código embebido directamente en el binario del Desktop.
5. **Autonomía del SDK:** El SDK de espacio de usuario (`userspace/sdk`) debe ser capaz de compilar de manera totalmente autónoma cualquier aplicación externa sin requerir dependencias no resueltas en el árbol de compilación del kernel.

---

## 2. Mapa Canónico de Memoria Virtual (x86_64)

Para erradicar colisiones entre segmentos de código, datos no inicializados (`.bss`), assets y pilas de ejecución, el espacio de direcciones virtual de 64 bits se particiona de forma estricta según el siguiente esquema:

```
0x0000_0000_0000_0000 - 0x0000_0000_3FFF_FFFF : [RESERVADO] Primer GiB (Protección Null-pointer / BIOS legacy)
0x0000_0000_4000_0000 - 0x0000_007F_FFFF_FFFF : Kernel Identity / Framebuffer / Tablas de Boot (Solo Ring 0)
0x0000_6000_0000_0000 - 0x0000_6000_0FFF_FFFF : USERSPACE: Código de la App (.text, .rodata, .data)
0x0000_6000_1000_0000 - 0x0000_6000_1FFF_FFFF : USERSPACE: Segmento BSS y Heap de la App (16 MiB)
0x0000_6000_2000_0000 - 0x0000_6000_3FFF_FFFF : USERSPACE: Bloque de Assets de la App (.mpk assets)
0x0000_6FFF_F000_0000 - 0x0000_6FFF_FFFF_FFFF : USERSPACE: Pila de Ejecución de Usuario (Grows Downwards)
0x0000_7000_0000_0000 - 0x0000_7000_FFFF_FFFF : USERSPACE: Buffers Compartidos / Shared Surfaces (Compositor)
0xFFFF_8000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF : Espacio Superior Canónico del Kernel (Paginación High-Half)
```

### Reglas de Carga del Formato MPK (`PackageLoader`)
1. **Separación Pila-BSS:** La pila del proceso de usuario **NO DEBE** ubicarse a continuación del código (`user_base + 0x400000`). La pila debe ubicarse en `0x0000_6FFF_FFFF_0000`, creciendo hacia abajo con páginas de guardia no presentes (`PROT_NONE`) para capturar desbordamientos de pila (*stack overflows*).
2. **Protección de Páginas:**
   - `.text`: `PAGE_USER | PAGE_EXECUTABLE | PAGE_PRESENT` (Solo lectura y ejecución).
   - `.rodata`: `PAGE_USER | PAGE_PRESENT` (Solo lectura, NX bit habilitado).
   - `.data`, `.bss`, heap y pila: `PAGE_USER | PAGE_WRITABLE | PAGE_PRESENT` (Lectura y escritura, NX habilitado).
3. **Cero Punteros Huérfanos:** Al cargar assets, estos deben residir en su propia ventana virtual mapeada de solo lectura, accesible para el proceso mediante el puntero entregado en `RDI` durante el inicio.

---

## 3. Arquitectura Canónica del Subsistema de Memoria (MM)

```
┌────────────────────────────────────────────────────────┐
│             KHeap Allocator (Ring 0)                   │
│      kmalloc() / kfree() - Segmentos con Magic 0xC0FFEE│
└──────────────────────────┬─────────────────────────────┘
                           │ (Solicita bloques contiguos)
┌──────────────────────────┴─────────────────────────────┐
│          Physical Memory Manager (PMM)                 │
│      PMM::AllocPage() / PMM::AllocContiguous()         │
│      PMM::FreePage()  / PMM::FreeContiguous()          │
│               (Control por Bitmap en RAM)              │
└──────────────────────────┬─────────────────────────────┘
                           │ (Páginas físicas)
┌──────────────────────────┴─────────────────────────────┐
│          Virtual Memory Manager (MMU)                  │
│      MMU::CreatePageTable() / MMU::DestroyPageTable()  │
│      MMU::MapPage() / MMU::UnmapPage()                 │
│         (Tablas PML4 -> PDPT -> PD -> PT)              │
└────────────────────────────────────────────────────────┘
```

### Invariantes Estrictos del Gestor de Memoria:
1. **Destrucción de Tablas de Páginas:**
   ```cpp
   // PROHIBIDO: kfree((void*)table_phys);
   // CORRECTO:
   void DestroyPageTable(uint64_t table_phys) {
       // 1. Recorrer y liberar todas las páginas de nivel PT, PD, PDPT asignadas por PMM
       // 2. Liberar la tabla raíz PML4 física
       PMM::FreePage((void*)table_phys);
   }
   ```
2. **Aislamiento de Heap entre Kernel y Usuario:**
   - La syscall `SYS_MALLOC` (o `sys_mmap` / `sys_brk`) debe asignar memoria **dentro del rango virtual del proceso** (`0x6000_1000_0000`), asignando páginas físicas desde el PMM y marcándolas con `PAGE_USER`.
   - Jamás debe devolverse un puntero de `kmalloc` a un proceso en Ring 3.

---

## 4. ABI Unificada de Syscalls y Protocolo de Validación

Morphic OS utiliza la instrucción nativa `SYSCALL` de x86-64 con conmutación de privilegios mediante los registros de modelo específico (**MSRs: STAR, LSTAR, SFMASK**) y la pila de supervisor indicada en el **TSS (RSP0)**.

### Registro y Convenciones de Paso de Parámetros
- `RAX`: Número de Syscall / Código de Retorno (Entero de 64 bits con signo).
- `RDI`: Argumento 1
- `RSI`: Argumento 2
- `RDX`: Argumento 3
- `R10`: Argumento 4 (en lugar de `RCX`, reservado por la instrucción `SYSCALL` para guardar el RIP de usuario)
- `R8` : Argumento 5
- `R9` : Argumento 6
- Registros callee-saved (`RBX`, `RSP`, `RBP`, `R12`-`R15`) deben ser estrictamente preservados por el manejador de interrupción/syscall en el kernel.

### Protocolo Obligatorio de Validación de Punteros de Usuario
Toda syscall que reciba un puntero proveniente de Ring 3 debe validarlo contra los límites del espacio de direcciones de usuario antes de desreferenciarlo:

```cpp
static inline bool IsValidUserRange(uint64_t addr, size_t size) {
    if (addr == 0 || size == 0) return false;
    // Evitar desbordamiento de enteros
    if (addr + size < addr) return false;
    // El rango debe pertenecer estrictamente al espacio de usuario (Lower Half)
    const uint64_t USER_SPACE_MIN = 0x0000_0000_4000_0000ULL;
    const uint64_t USER_SPACE_MAX = 0x0000_7FFF_FFFF_FFFFULL;
    return (addr >= USER_SPACE_MIN && (addr + size) <= USER_SPACE_MAX);
}
```

### Tabla Canónica de Syscalls

| ID | Nombre | Argumentos | Retorno | Propósito |
|---|---|---|---|---|
| `0` | `SYS_EXIT` | `rdi = exit_code` | Nunca retorna | Termina el proceso actual y libera sus recursos. |
| `1` | `SYS_WRITE` | `rdi = fd, rsi = buf, rdx = count` | bytes escritos / error | Escritura a consola o descriptor de archivo. |
| `2` | `SYS_READ` | `rdi = fd, rsi = buf, rdx = count` | bytes leídos / error | Lectura desde stdin o descriptor de archivo. |
| `4` | `SYS_MALLOC` | `rdi = size_bytes` | Dirección virtual de usuario | Reserva memoria virtual de usuario mapeada con PAGE_USER. |
| `5` | `SYS_OPEN` | `rdi = const char* path, rsi = flags` | `fd` / error (-1) | Abre un archivo en el VFS y devuelve su descriptor. |
| `6` | `SYS_CLOSE` | `rdi = int fd` | `0` / error (-1) | Cierra un descriptor de archivo abierto. |
| `10`| `SYS_UPDATE_SCREEN` | Ninguno | `0` | Presentación del backbuffer global. |
| `11`| `SYS_GET_SCREEN_INFO`| Ninguno | `(width << 32) \| height` | Obtiene dimensiones actuales de la pantalla. |
| `12`| `SYS_BEEP` | `rdi = freq_hz, rsi = duration_ms` | `0` | Tono de alerta mediante PC Speaker. |
| `13`| `SYS_SLEEP` | `rdi = ms` | `0` | Suspende el hilo llamante durante N milisegundos. |
| `20`| `SYS_GET_TIME_MS` | Ninguno | Uptime en milisegundos | Marca de tiempo monotónica desde arranque. |
| `21`| `SYS_GET_EVENT` | `rdi = OSEvent*` | `1` si hay evento, `0` si vacío | Extracción sin bloqueo de la cola de eventos del proceso. |
| `50`| `SYS_VIDEO_MAP` | Ninguno | Puntero virtual al framebuffer | Mapeo directo del backbuffer de DRM (solo Compositor). |
| `51`| `SYS_VIDEO_FLIP` | `rdi = buffer_virt` | `1` (VSync OK) / `0` | Intercambio de pantalla con sincronización vertical. |
| `53`| `SYS_ALLOC_BACKBUFFER` | `rdi = size_bytes` | Puntero virtual a buffer RAM | Reserva memoria RAM cacheable para renderizado gráfico. |
| `54`| `SYS_VIDEO_FLIP_RECT` | `rdi = buf, rsi = pack(x,y), rdx = pack(w,h)` | `1` / `0` | Volcado parcial acelerado por región sucia (*dirty rect*). |
| `55`| `SYS_GET_RTC_DATETIME`| `rdi = MorphicDateTime*` | `1` / `0` | Lectura segura del reloj CMOS / RTC del sistema. |
| `56`| `SYS_GET_SYSTEM_INFO` | `rdi = MorphicSystemInfo*` | `1` / `0` | Información de hardware (CPU, memoria total/libre). |
| `60`| `SYS_SPAWN` | `rdi = const char* mpk_path` | `PID` / error | Carga y ejecución de un paquete `.mpk` en nuevo proceso. |
| `62`| `SYS_CREATE_WINDOW` | `rdi = w, rsi = h, rdx = flags` | Puntero a buffer de ventana | Reserva superficie de renderizado para app cliente. |
| `63`| `SYS_REGISTER_COMPOSITOR`| Ninguno | `0` | Registra el PID llamante como gestor exclusivo de pantalla. |
| `65`| `SYS_POST_MESSAGE` | `rdi = dest_pid, rsi = OSEvent*` | `0` / error | IPC entre procesos (Compositor <-> Clientes). |
| `70`| `SYS_SET_KEYMAP` | `rdi = const char* code` | `0` / error | Cambia mapa de teclado activo ("US", "ES", "LA"). |

> [!CAUTION]
> El archivo `kernel/api/morphic_api.cpp` y sus definiciones huérfanas quedan formalmente **deprecados y marcados para eliminación**. No deben agregarse nuevas funciones en dicho módulo.

---

## 5. Arquitectura del Entorno Gráfico y Compositor

### Desacoplamiento Compositor / Aplicaciones
El sistema gráfico opera bajo el modelo de **superficies compartidas (Shared Surfaces / Client-Side Rendering)**, análogo a los compositores modernos:
1. **Compositor de Escritorio (`desktop.mpk` / `compositor.mpk`):**
   - Es el único proceso de espacio de usuario con permiso para invocar `SYS_REGISTER_COMPOSITOR` y `SYS_VIDEO_MAP`.
   - Gestiona el fondo de pantalla, barra de tareas, menú de inicio, reloj, z-order de ventanas y decoraciones (marcos, sombras, botones cerrar/minimizar).
   - Recibe eventos crudos del kernel (`SYS_GET_EVENT`) y los redirige al cliente enfocado mediante IPC (`SYS_POST_MESSAGE`).
2. **Aplicaciones Clientes (Calculadora, Terminal, Editor, etc.):**
   - **JAMÁS** deben compilarse incrustadas dentro del código fuente de `desktop.cpp`.
   - Solicitan una superficie de ventana con `SYS_CREATE_WINDOW(w, h)`.
   - Dibujan en su propio backbuffer local y notifican al compositor cuando un cuadro está listo (`SYS_VIDEO_FLIP`).

---

## 6. Estándar de Construcción (Build System) e Infraestructura

1. **Autonomía de `app.mk`:**
   - La regla para compilar `entry.o` y `syscalls.o` debe estar presente en `userspace/sdk/app.mk`. Si los objetos no existen en el SDK, `app.mk` debe ensamblarlos automáticamente a partir de sus fuentes ASM sin requerir que se ejecute el Makefile raíz del kernel.
2. **Higiene del Repositorio:**
   - Queda estrictamente prohibido versionar en el repositorio archivos de desensamblado masivo o volcados crudos de depuración (ej. `desktop_asm.txt`, logs `.log`, imágenes de disco temporales `.img` no canónicas).
3. **Imágenes de Salida y Scripts de Ejecución:**
   - `run_direct.sh` debe verificar inteligentemente si existe `morphic_os.iso` o `morphic.img` antes de abortar, ofreciendo fallback transparente entre ambos formatos.
