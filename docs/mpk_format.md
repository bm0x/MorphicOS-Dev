# .mpk (MPK1) — Morphic Package Format (actual)

> Estado: **en uso**.
>
> Nota: el documento [docs/mapp_format.md](docs/mapp_format.md) queda **deprecado** (no refleja el sistema actual).

## ¿Qué es un MPK?

Un `.mpk` es un contenedor binario **muy simple** usado por Morphic para empaquetar aplicaciones de userspace.

En el estado actual del proyecto:

- El “código” es un **binario plano** (`--oformat binary`), no ELF.
- El kernel lo carga en memoria, mapea páginas a userspace y hace un salto a Ring 3.
- A `main(void* assets_ptr)` se le pasa un puntero a un bloque de **assets** (bytes crudos).

## Endianness y tamaños

- Endianness: **little-endian**.
- Campos del header: `uint32_t`.

## Estructura en disco

Layout actual generado por [tools/mpk_pack.py](../tools/mpk_pack.py):

```
+------------------------------+
| Header (64 bytes)            |
|  - MPKHeader (24 bytes)      |
|  - padding/reserved          |
+------------------------------+
| Manifest (opcional)          |
|  - bytes crudos              |
|  - empieza en manifest_off   |
|  - termina en code_off       |
+------------------------------+
| Code blob (code_size bytes)  |
+------------------------------+
| Assets blob (assets_size)    |
+------------------------------+
```

El kernel valida y usa offsets/tamaños para acotar lecturas.

## Header (MPKHeader)

Definido en [kernel/core/loader.h](../kernel/core/loader.h):

```c
struct MPKHeader {
    uint8_t  magic[4];      // "MPK1"
    uint32_t manifest_off;  // actualmente no usado (0)
    uint32_t code_off;      // típicamente 64
    uint32_t code_size;
    uint32_t assets_off;    // code_off + code_size
    uint32_t assets_size;
};
```

Notas importantes:

- `magic` debe ser exactamente `MPK1`.
- `manifest_off` puede ser **0** (sin manifest) o apuntar al bloque de manifest.
- El kernel lee `sizeof(MPKHeader)` (24 bytes) pero el packer **rellena a 64 bytes** por compatibilidad futura.

### Campo `total_size` (convención en padding)

Sin romper el header de 24 bytes, el packer escribe un `uint32_t total_size` en el **padding** del header:

- Offset: byte **24** dentro del header de 64 bytes.
- Valor: tamaño total del archivo `.mpk` en bytes.
- Si es `0`, se considera “no presente”.

El loader puede validarlo para detectar MPKs truncados o con offsets corruptos.

## Bloque de Code

- Es un blob ejecutable “tal cual”.
- Se produce actualmente con:
  - `clang -target x86_64-elf ...`
  - `ld -T userspace/linker.ld ... --oformat binary`

El entrypoint es el primer byte del blob (el loader salta a la base mapeada).

Si existe un manifest, el code empieza en `code_off` (después del manifest).

## Bloque de Assets (MPK1 actual)

En MPK1 actual, el packer concatena assets como bytes crudos:

- No existe directorio/tabla de archivos estándar dentro del assets block.
- Por lo tanto, si una app quiere múltiples assets, hoy necesita:
  - o bien conocer offsets/tamaños “por convención”,
  - o bien definir su propio mini-formato dentro del assets block.

### Convención del SDK (recomendada)

Sin cambiar el formato del MPK, el packer puede generar un header con offsets/tamaños para la app.

Ver “SDK MPK” en [docs/mpk_sdk.md](mpk_sdk.md).

## Carga en kernel (resumen)

Implementación en [kernel/core/loader.cpp](../kernel/core/loader.cpp):

- Abre el archivo con VFS.
- Lee `MPKHeader`, valida `magic` y valida bounds de `code_off/code_size/assets_off/assets_size`.
- Reserva memoria física contigua suficiente para code+assets+stack.
- Copia code y assets a esa región.
- Mapea las páginas a un VA de userspace (base `0x600000000000`).
- Salta a userspace con `JumpToUser(entry, stack, assets_ptr)`.

## ABI de entrada (userspace)

El startup en [userspace/entry.asm](../userspace/entry.asm) llama a:

```c
extern "C" int main(void* assets_ptr);
```

- `assets_ptr` apunta al inicio del **assets blob** (no al header del MPK).
- Si `assets_size == 0`, el puntero puede ser válido pero no hay datos útiles.

## Herramientas

- Packer: [tools/mpk_pack.py](../tools/mpk_pack.py)
- Embed en kernel: [tools/bin2h.py](../tools/bin2h.py) genera `kernel/fs/desktop_mpk.cpp`.

### Manifest (opcional)

El packer puede incluir un manifest (texto o binario) con:

```bash
python3 tools/mpk_pack.py out/app.mpk out/app.bin \
  --manifest userspace/apps/app/manifest.txt \
  assets/a.bin assets/b.raw
```

En el estado actual, el kernel solo lo valida por offsets; la interpretación (nombre/versión/permisos) se puede definir en una iteración futura.

