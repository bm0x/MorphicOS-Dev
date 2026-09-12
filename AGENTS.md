# Guía Operativa de Ingeniería y Directrices para Agentes de IA en Morphic OS

> **Documento Oficial de Instrucción para Agentes Autónomos (AGENTS.md)**  
> **Referencia Arquitectónica Principal:** [docs/DEVELOPMENT_ARCHITECTURE_SPEC.md](file:///home/ubuntu/snap/MorphicOS-Dev/docs/DEVELOPMENT_ARCHITECTURE_SPEC.md)  
> **Documento de Presentación:** [README.md](file:///home/ubuntu/snap/MorphicOS-Dev/README.md)

Este documento define el **protocolo vinculante de desarrollo, análisis y codificación** que todo agente de Inteligencia Artificial (o desarrollador de sistemas) **DEBE cumplir sin excepción** al inspeccionar, diseñar, refactorizar o agregar código en Morphic OS.

---

## 1. Regla de Oro del Agente

> [!IMPORTANT]
> **Antes de escribir o modificar una sola línea de código en el kernel o userspace**, el agente DEBE consultar y respetar la [Especificación Maestra de Arquitectura](file:///home/ubuntu/snap/MorphicOS-Dev/docs/DEVELOPMENT_ARCHITECTURE_SPEC.md). Cualquier código que viole el aislamiento de privilegios, invente llamadas al sistema huérfanas o introduzca acoplamientos monolíticos será considerado **código defectuoso de inmediato rechazo**.

---

## 2. Invariantes de Seguridad y Aislamiento de Memoria

### 2.1. Separación Ring 0 (Kernel) vs Ring 3 (Userspace)
1. **Cero Fugas de KHeap:** Jamás retornar un puntero de `kmalloc()` a un proceso de usuario mediante llamadas como `SYS_MALLOC`. Toda memoria de usuario debe ser asignada en el espacio virtual del proceso (`0x6000_1000_0000`) a través de páginas físicas obtenidas de `PMM` y mapeadas con la bandera `PAGE_USER`.
2. **Validación de Punteros en Syscalls:** Toda syscall que reciba un puntero (`arg1`, `arg2`, `arg3`) debe verificar que la dirección caiga estrictamente dentro del rango de usuario antes de leer o escribir en ella. El kernel **nunca debe sufrir un Page Fault en Ring 0** por culpa de un puntero inválido provisto por una aplicación.
3. **Pilas Aisladas:** La pila de usuario debe ubicarse en su propia región virtual alta (`0x0000_6FFF_FFFF_0000`), nunca dentro del rango de 4MB contiguo al código para evitar colisiones con la sección `.bss` o assets.

### 2.2. Disciplina del Asignador de Memoria
- `PMM::AllocPage()` y `PMM::AllocContiguous()` operan sobre memoria física bruta. Su contraparte es **únicamente** `PMM::FreePage()`.
- `kmalloc()` opera sobre el heap del kernel (`KHeap`), utilizando cabeceras con magic `0xC0FFEE`. Su contraparte es **únicamente** `kfree()`.
- **PROHIBIDO:** Llamar a `kfree()` sobre direcciones devueltas por `PMM::AllocPage()` o tablas de páginas (`PML4`, `PDPT`, etc.).

---

## 3. Arquitectura de Aplicaciones y Entorno Gráfico

1. **Prohibición de Clases Embebidas en el Desktop:**
   - Queda estrictamente prohibido incrustar aplicaciones (Calculadora, Terminal, Juegos, etc.) como clases directas dentro de `userspace/apps/desktop/desktop.cpp`.
   - Cada aplicación debe ser un proyecto `.mpk` autónomo en su propio subdirectorio de `userspace/apps/`, compilado con el SDK y ejecutado en un proceso separado (`SYS_SPAWN`).
2. **Protocolo del Compositor:**
   - El Compositor de usuario es responsable del marco, barra de tareas y decoraciones.
   - Las aplicaciones clientes renderizan en sus propias superficies provistas por `SYS_CREATE_WINDOW` y se sincronizan vía IPC (`SYS_POST_MESSAGE`, `SYS_VIDEO_FLIP`).
3. **Eliminación de Código Muerto:**
   - No mantener módulos "maqueta" o stubs abandonados como `kernel/api/morphic_api.cpp`. El despachador canónico es `kernel/hal/arch/x86_64/syscall.cpp`.

---

## 4. Estándar del SDK y Sistema de Construcción

1. **Autonomía de Compilación:**
   - El archivo `userspace/sdk/app.mk` debe permitir que cualquier app se compile de forma independiente sin requerir haber ejecutado previamente un `make` en la raíz del kernel. Las reglas para generar `entry.o` y `syscalls.o` deben estar resueltas dentro del SDK.
2. **Higiene del Repositorio:**
   - No agregar volcados crudos de desensamblador (`*_asm.txt`), ejecutables binarios sin rastreo, ni logs masivos temporales (`boot.log`) al control de versiones.
3. **Sincronización Documental:**
   - Si se añade, modifica o elimina una syscall, se deben actualizar simultáneamente:
     - `kernel/hal/arch/x86_64/syscall.h`
     - `kernel/hal/arch/x86_64/syscall.cpp`
     - `userspace/sdk/morphic_syscalls.h`
     - `userspace/syscalls.asm`
     - [docs/DEVELOPMENT_ARCHITECTURE_SPEC.md](file:///home/ubuntu/snap/MorphicOS-Dev/docs/DEVELOPMENT_ARCHITECTURE_SPEC.md)
     - [README.md](file:///home/ubuntu/snap/MorphicOS-Dev/README.md)

---

## 5. Lista de Verificación (Checklist) Obligatoria para Agentes

Antes de dar por completada cualquier tarea en el repositorio, el agente debe verificar:

- [ ] **1. Compilación Limpia del Kernel:** `make clean && make` debe compilar sin errores.
- [ ] **2. Generación de Imagen ISO:** `make iso` debe producir `morphic_os.iso` y `morphic.img` válidos.
- [ ] **3. Compilación Autónoma del SDK:** Verificar que entrar a `userspace/apps/calculator` (o similar) y ejecutar `make` compile el paquete `.mpk` de forma autónoma.
- [ ] **4. Integridad de Paginación:** Ninguna función destructora de tablas de páginas llama a `kfree()`.
- [ ] **5. Aislamiento de Memoria:** Ninguna syscall expone memoria física o KHeap de Ring 0 a Ring 3 sin mapeo explícito `PAGE_USER`.
- [ ] **6. Verificación de Ejecución:** Si se modificó el arranque o drivers, verificar en QEMU mediante `./run_direct.sh` o inspeccionar la salida de depuración COM1/serial.
