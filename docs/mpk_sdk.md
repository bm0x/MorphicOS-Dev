# SDK MPK (v2)

Objetivo: crear Apps `.mpk` **rápido** y sin pelearse con offsets manuales.

Este SDK es compatible con el MPK1 actual (header + code + assets).

## Novedades (v2)

- **Asset Table**: El packer ahora inyecta automáticamente una tabla de índice al inicio del bloque de assets.
- **Búsqueda por nombre**: Puedes usar `mpk_find_asset(ptr, "archivo.png", &size)` en tiempo de ejecución.
- **Reglas de Build**: Archivo `userspace/sdk/app.mk` para simplificar Makefiles.

## Uso Básico (Recomendado)

### 1. Estructura del Proyecto

```
my_app/
  main.cpp
  assets/
    icon.png
    data.txt
  Makefile
```

### 2. Makefile Simplificado

```makefile
APP_NAME = my_app
APP_SRCS = main.cpp
APP_ASSETS = assets/icon.png assets/data.txt

# Ajustar ruta a la raíz de Morphic
MORPHIC_ROOT = ../../..
include $(MORPHIC_ROOT)/userspace/sdk/app.mk
```

### 3. Código (main.cpp)

```cpp
#include "mpk.h"

extern "C" int main(void* assets_ptr) {
    // Opción A: Buscar por nombre (Flexible)
    uint32_t size;
    const uint8_t* data = mpk_find_asset(assets_ptr, "icon.png", &size);
    
    if (data) {
        // Usar asset...
    }

    // Opción B: Usar macros generadas (Más rápido, requiere incluir mpk_assets.h)
    // #include "mpk_assets.h"
    // const uint8_t* fast_data = mpk_asset_ptr(assets_ptr, MY_APP_MPK_ASSET_ICON_PNG_OFFSET);

    return 0;
}
```

## Detalles Técnicos

### Asset Table (Índice)

El bloque de assets comienza con:

1.  **Header**: Magic "ASST" (4 bytes) + Count (4 bytes).
2.  **Entradas**: Array de structs `MPKAssetEntry` (72 bytes c/u).
    -   Name (64 bytes)
    -   Offset (4 bytes)
    -   Size (4 bytes)
3.  **Datos**: Los archivos concatenados (alineados).

El SDK (`mpk.h`) maneja esto transparente con `mpk_find_asset`.

### Packer

El packer [tools/mpk_pack.py](../tools/mpk_pack.py) genera automáticamente esta tabla.
Sigue soportando `--gen-header` para quienes prefieren macros en tiempo de compilación.

