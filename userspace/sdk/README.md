# Morphic OS - Userspace SDK

SDK para desarrollar aplicaciones userspace empaquetadas como `.mpk` para Morphic OS.

## Arquitectura del SDK

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          APLICACIÓN (.mpk)                              │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                        Tu Código (main.cpp)                       │  │
│  │                int main(void* assets_ptr) { ... }                 │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                   │                                      │
│  ┌───────────────────────────────┴───────────────────────────────────┐  │
│  │                      MorphicAPI (morphic_api.h)                   │  │
│  │          Window │ Graphics │ Helpers (memcpy, strlen)             │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                   │                                      │
│  ┌───────────────────────────────┴───────────────────────────────────┐  │
│  │                       SDK Components                              │  │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐  │  │
│  │  │ runtime.cpp │ │   mpk.h     │ │   gui/      │ │system_info.h│  │  │
│  │  │  C++ RT     │ │Asset Lookup │ │ Compositor  │ │  Structs    │  │  │
│  │  │ new/delete  │ │             │ │             │ │             │  │  │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                   │                                      │
│  ┌───────────────────────────────┴───────────────────────────────────┐  │
│  │                     Syscall Layer (syscalls.asm)                  │  │
│  │         sys_video_flip │ sys_get_event │ sys_sleep │ ...          │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                   │                                      │
│  ┌───────────────────────────────┴───────────────────────────────────┐  │
│  │                        Entry Point (entry.asm)                    │  │
│  │              _start → align stack → call main(assets)             │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                              SYSCALL ABI
                        (RAX=num, RDI/RSI/RDX=args)
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           KERNEL (Ring 0)                               │
└─────────────────────────────────────────────────────────────────────────┘
```

## Archivos del SDK

| Archivo | Descripción |
|---------|-------------|
| `morphic_syscalls.h` | Prototipos C de los stubs de syscalls |
| `morphic_api.h` | Clases `Window`, `Graphics`, `Helpers` para desarrollo de apps |
| `morphic_system_info.h` | Structs compartidos: `MorphicDateTime`, `MorphicSystemInfo` |
| `mpk.h` | Helpers para acceder a assets embebidos en el MPK |
| `runtime.cpp` | Runtime C++ mínimo (`new`/`delete`, `__cxa_pure_virtual`) |
| `app.mk` | Makefile reutilizable para compilar apps MPK |
| `gui/` | Biblioteca de composición/widgets (libmorphic_gui.a) |

## Punto de Entrada

El loader del kernel llama a `main(void* assets_ptr)`:

```cpp
// Tu app debe definir esta función
int main(void* assets_ptr) {
    // assets_ptr apunta al blob de assets del MPK
    // Si tu app no tiene assets, puede ser nullptr
    
    // Tu código aquí...
    
    return 0; // (actualmente ignorado)
}
```

El entry point real está en [entry.asm](../entry.asm):
```asm
_start:
    and rsp, -16        ; Alinear stack a 16 bytes (System V ABI)
    call main           ; RDI = assets_ptr (ya configurado por loader)
.halt:
    jmp .halt           ; Loop infinito si main retorna
```

## Syscalls Disponibles

Los stubs están implementados en [syscalls.asm](../syscalls.asm). Incluye `morphic_syscalls.h` para usarlos:

### Video/Gráficos

| Syscall | Número | Descripción |
|---------|--------|-------------|
| `sys_get_screen_info()` | 11 | Retorna `(width << 32) \| height` |
| `sys_video_map()` | 50 | Mapea framebuffer a userspace (fullscreen legacy) |
| `sys_create_window(w, h, flags)` | 62 | Crea ventana con tamaño específico |
| `sys_video_flip(backbuffer)` | 51 | Presenta backbuffer completo |
| `sys_video_flip_rect(buf, xy, wh)` | 54 | Presenta solo rectángulo (dirty-rect) |
| `sys_alloc_backbuffer(size)` | 53 | Aloca backbuffer cacheable |

### Input/Eventos

| Syscall | Número | Descripción |
|---------|--------|-------------|
| `sys_get_event(OSEvent*)` | 21 | Obtiene evento de la cola (1=ok, 0=vacía) |
| `sys_input_poll(out)` | 52 | Poll de input (alternativo) |

### Tiempo

| Syscall | Número | Descripción |
|---------|--------|-------------|
| `sys_get_time_ms()` | 20 | Milisegundos desde boot (PIT 1000Hz) |
| `sys_sleep(ms)` | 13 | Dormir N milisegundos |
| `sys_get_rtc_datetime(MorphicDateTime*)` | 55 | Fecha/hora del RTC |

### Sistema

| Syscall | Número | Descripción |
|---------|--------|-------------|
| `sys_get_system_info(MorphicSystemInfo*)` | 56 | Info de CPU/memoria/display |
| `sys_debug_print(msg)` | 61 | Imprime string a consola kernel |
| `sys_spawn(path)` | 60 | Lanza otra app MPK |

### Compositor (Avanzado)

| Syscall | Número | Descripción |
|---------|--------|-------------|
| `sys_register_compositor()` | 63 | Registra app como compositor |
| `sys_map_window(windowId)` | 64 | Mapea buffer de otra ventana |
| `sys_post_message(pid, event)` | 65 | Envía mensaje a otro proceso |

## Estructuras Compartidas

### OSEvent (shared/os_event.h)

```cpp
struct OSEvent {
    enum Type {
        NONE = 0,
        MOUSE_MOVE = 1,
        MOUSE_CLICK = 2,
        KEY_PRESS = 3,
        KEY_RELEASE = 4,
        WINDOW_CREATED = 5,
        WINDOW_DESTROYED = 6
    };
    uint32_t type;
    int32_t dx, dy;        // Posición/delta del mouse
    uint32_t buttons;      // Botones del mouse o modificadores
    uint32_t scancode;     // Scancode del teclado
    uint32_t ascii;        // Carácter ASCII
};
```

### MorphicDateTime (morphic_system_info.h)

```cpp
struct MorphicDateTime {
    uint16_t year;
    uint8_t month, day;
    uint8_t hour, minute, second;
    uint8_t valid;         // 1 si RTC read OK
};
```

### MorphicSystemInfo (morphic_system_info.h)

```cpp
struct MorphicSystemInfo {
    char cpu_vendor[13];
    char cpu_brand[49];
    uint64_t total_mem_bytes, free_mem_bytes;
    uint32_t fb_width, fb_height, fb_pitch, fb_bpp;
    uint64_t disk_total_bytes, disk_free_bytes;
};
```

## MorphicAPI - Framework de Alto Nivel

### Clase Window

Base para crear aplicaciones con ventana:

```cpp
#include "morphic_api.h"

class MiApp : public MorphicAPI::Window {
public:
    MiApp() : Window(640, 480) {}  // Ventana 640x480
    // MiApp() : Window() {}       // Fullscreen

    void OnUpdate() override {
        // Lógica de actualización
    }
    
    void OnRender(MorphicAPI::Graphics& g) override {
        g.Clear(COLOR_BLACK);
        g.FillRect(100, 100, 200, 150, COLOR_BLUE);
    }
    
    void OnKeyDown(char c) override {
        // Manejar tecla
    }
    
    void OnMouseDown(int x, int y, int btn) override {
        // Manejar click
    }
    
    void OnMouseMove(int x, int y) override {
        // Manejar movimiento
    }
};

int main(void* assets) {
    MiApp app;
    app.Run();  // Loop principal
    return 0;
}
```

### Clase Graphics

Primitivas de dibujo:

```cpp
void Clear(uint32_t color);
void PutPixel(int x, int y, uint32_t color);
void FillRect(int x, int y, int w, int h, uint32_t color);
```

### Clase Helpers

Funciones de utilidad (sin stdlib):

```cpp
static void* memset(void* dest, int c, size_t n);
static void* memcpy(void* dest, const void* src, size_t n);
static size_t strlen(const char* s);
```

## Assets Embebidos (MPK)

### Estructura del Asset Table

El packer genera una tabla de assets al inicio del blob:

```cpp
struct MPKAssetTable {
    uint8_t magic[4];    // "ASST"
    uint32_t count;      // Número de assets
};

struct MPKAssetEntry {
    char name[64];       // Nombre del asset
    uint32_t offset;     // Offset desde inicio del blob
    uint32_t size;       // Tamaño en bytes
};
```

### Acceso a Assets

**Opción 1: Lookup por nombre (runtime)**

```cpp
#include "mpk.h"

int main(void* assets) {
    uint32_t size;
    const uint8_t* icon = mpk_find_asset(assets, "icon.bmp", &size);
    if (icon) {
        // Usar icon...
    }
    return 0;
}
```

**Opción 2: Offsets compilados (más rápido)**

```bash
python3 tools/mpk_pack.py out/app.mpk out/app.bin \
  assets/icon.bmp assets/font.bin \
  --gen-header mpk_assets.h \
  --prefix MYAPP \
  --align 16
```

Genera `mpk_assets.h`:
```cpp
#define MYAPP_MPK_ASSET_ICON_BMP_OFFSET  128
#define MYAPP_MPK_ASSET_ICON_BMP_SIZE    4096
#define MYAPP_MPK_ASSET_FONT_BIN_OFFSET  4224
#define MYAPP_MPK_ASSET_FONT_BIN_SIZE    2048
```

Uso:
```cpp
#include "mpk_assets.h"
#include "mpk.h"

const uint8_t* icon = mpk_asset_ptr(assets, MYAPP_MPK_ASSET_ICON_BMP_OFFSET);
```

## Build System

### Usando app.mk (Recomendado)

Crea un `Makefile` en tu carpeta de app:

```makefile
APP_NAME = miapp
APP_SRCS = main.cpp utils.cpp
APP_ASSETS = assets/icon.bmp assets/font.bin

include ../../sdk/app.mk
```

Luego:
```bash
make        # Genera miapp.mpk
make clean  # Limpia
```

### Build Manual

```bash
# 1. Compilar C++
clang++ -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
        -mcmodel=large -mno-red-zone -nostdlib \
        -I shared -I userspace/sdk -c main.cpp -o main.o

# 2. Compilar runtime (si no existe)
clang++ -target x86_64-elf -ffreestanding -fno-rtti -fno-exceptions \
        -mcmodel=large -mno-red-zone -nostdlib \
        -c userspace/sdk/runtime.cpp -o runtime.o

# 3. Linkear
ld.lld -T userspace/linker.ld -z max-page-size=4096 \
       -o app.bin userspace/entry.o runtime.o main.o userspace/syscalls.o \
       --oformat binary

# 4. Empaquetar
python3 tools/mpk_pack.py app.mpk app.bin [assets...]
```

### Flags de Compilación

| Flag | Propósito |
|------|-----------|
| `-target x86_64-elf` | Target ELF x86_64 |
| `-ffreestanding` | Sin hosted environment |
| `-fno-rtti` | Sin RTTI (reduce tamaño) |
| `-fno-exceptions` | Sin excepciones C++ |
| `-mcmodel=large` | Modelo de código grande (direcciones > 2GB) |
| `-mno-red-zone` | Requerido para código que maneja interrupciones |
| `-nostdlib` | Sin biblioteca estándar |

## Runtime C++ (runtime.cpp)

El runtime provee soporte mínimo para C++:

```cpp
// Heap estático de 1MB para new/delete
void* operator new(size_t size);
void operator delete(void* p) noexcept;
void operator delete(void* p, size_t) noexcept;

// Llamado si se invoca un método virtual puro
void __cxa_pure_virtual();
```

> ⚠️ **Limitación**: El heap es un bump allocator de 1MB. `delete` no libera memoria.
> Para apps complejas, considera implementar un allocator propio.

## Ejemplo Completo

```cpp
// main.cpp - Aplicación de ejemplo
#include "morphic_api.h"
#include "mpk.h"

class DemoApp : public MorphicAPI::Window {
    int boxX = 100, boxY = 100;
    
public:
    DemoApp() : Window(800, 600) {}
    
    void OnUpdate() override {
        // Actualizar estado
    }
    
    void OnRender(MorphicAPI::Graphics& g) override {
        g.Clear(0xFF1E1E2E);  // Fondo oscuro
        g.FillRect(boxX, boxY, 100, 100, 0xFF89B4FA);  // Caja azul
    }
    
    void OnKeyDown(char c) override {
        switch(c) {
            case 'w': boxY -= 10; break;
            case 's': boxY += 10; break;
            case 'a': boxX -= 10; break;
            case 'd': boxX += 10; break;
        }
    }
    
    void OnMouseDown(int x, int y, int btn) override {
        boxX = x - 50;
        boxY = y - 50;
    }
};

int main(void* assets) {
    DemoApp app;
    app.Run();
    return 0;
}
```

## Estructura de Proyecto Recomendada

```
userspace/apps/miapp/
├── Makefile           # include ../../sdk/app.mk
├── manifest.txt       # Metadatos (opcional)
├── main.cpp           # Código principal
├── utils.cpp          # Código adicional
├── assets/
│   ├── icon.bmp
│   └── font.bin
└── mpk_assets.h       # Generado por mpk_pack.py
```

## Tips y Mejores Prácticas

1. **Dirty Rectangles**: Usa `sys_video_flip_rect()` en vez de `sys_video_flip()` para mejor rendimiento.

2. **Event Loop**: Procesa todos los eventos disponibles antes de renderizar:
   ```cpp
   while (sys_get_event(&ev)) { /* procesar */ }
   OnRender(g);
   sys_video_flip(backbuffer);
   ```

3. **Sleep**: Usa `sys_sleep(16)` para ~60 FPS sin quemar CPU.

4. **Debug**: Usa `sys_debug_print("mensaje\n")` para imprimir a la consola del kernel.

5. **Assets grandes**: Alinea assets a 16 bytes (`--align 16`) para mejor rendimiento.

## Limitaciones Conocidas

- Heap userspace limitado a 1MB estático (bump allocator)
- No hay soporte de threading en userspace
- Sin soporte de archivos (solo assets embebidos)
- El `delete` no libera memoria realmente
- Renderizado por software únicamente

