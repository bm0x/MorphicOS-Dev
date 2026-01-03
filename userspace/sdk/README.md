# Userspace SDK (MVP)

Este folder agrupa headers/utilidades mínimas para escribir Apps userspace empaquetadas como `.mpk`.

## Punto de entrada

El loader llama a `main(void* assets_ptr)` (ver [userspace/entry.asm](../entry.asm)).

## Syscalls

Las stubs están en [userspace/syscalls.asm](../syscalls.asm).

En C/C++, podés incluir:

- `userspace/sdk/morphic_syscalls.h` (prototipos)

Y (si vas a usar assets empaquetados):

- `userspace/sdk/mpk.h` (helper `mpk_asset_ptr`)

## Assets (recomendado)

Como MPK1 no trae directorio interno, lo más práctico es generar offsets/tamaños con el packer:

```bash
python3 tools/mpk_pack.py out/app.mpk out/app.bin \
  assets/a.bin assets/b.raw \
  --gen-header userspace/apps/app/mpk_assets.h \
  --prefix APP \
  --align 16
```

Eso produce macros `APP_MPK_ASSET_*` que tu app puede usar.

## Build

El repo hoy compila el Desktop con reglas específicas en el Makefile.

Para nuevas apps MPK, la forma más directa (MVP) es replicar el patrón:

- compilar `.cpp` con `-target x86_64-elf -ffreestanding -nostdlib ...`
- linkear con `ld -T userspace/linker.ld ... --oformat binary`
- empaquetar con `tools/mpk_pack.py`

Más adelante, el SDK puede evolucionar a targets genéricos tipo `make app APP=name`.
