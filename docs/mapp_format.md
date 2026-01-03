# .mapp - Morphic Application Package Format (DEPRECADO)

> Este documento está **deprecado**.
>
> El sistema actual usa `.mpk` (MPK1) para empaquetar aplicaciones de userspace.
> Ver: [docs/mpk_format.md](mpk_format.md) y [docs/mpk_sdk.md](mpk_sdk.md).

## Overview

`.mapp` is a simple archive format for Morphic OS applications. It bundles code, assets, and metadata in a single file.

---

## File Structure

```
app.mapp (ZIP-based archive)
├── manifest.json     # Required: App metadata
├── icon.bmp          # Optional: 32x32 icon
├── main.[lua|wasm]   # Required: Entry point
└── assets/           # Optional: Resources
    ├── images/
    ├── sounds/
    └── data/
```

---

## manifest.json

```json
{
  "name": "Calculator",
  "version": "1.0.0",
  "author": "Morphic Dev",
  "description": "Simple calculator app",
  
  "entry": "main.lua",
  "runtime": "lua",
  
  "permissions": [
    "graphics",
    "input",
    "audio",
    "filesystem"
  ],
  
  "memory": {
    "heap": "4MB",
    "priority": "normal"
  },
  
  "window": {
    "width": 320,
    "height": 240,
    "resizable": false,
    "title": "Calculator"
  }
}
```

---

## Runtime Types

| Runtime | Extension | Description |
|---------|-----------|-------------|
| `lua` | .lua | Lua 5.4 interpreter |
| `wasm` | .wasm | WebAssembly module |
| `native` | .bin | ARM64/x86_64 binary |
| `morphic` | .mcl | MCL script |

---

## Permissions

| Permission | Syscalls Allowed |
|------------|------------------|
| `graphics` | GFX_DRAW, GFX_BLIT, GFX_FLIP |
| `audio` | AUDIO_PLAY, AUDIO_STOP |
| `input` | INPUT_POLL, INPUT_WAIT |
| `filesystem` | OPEN, READ, WRITE, CLOSE |
| `network` | (Future) |
| `system` | (Restricted) |

---

## Memory Priority

| Value | Level | Description |
|-------|-------|-------------|
| `critical` | 0 | Reserved for system |
| `high` | 1 | UI/Compositor priority |
| `normal` | 2 | Default applications |
| `low` | 3 | Background tasks |

---

## Loading Process

1. Kernel reads `manifest.json`
2. Validates permissions
3. Allocates UserHeap with requested size/priority
4. Loads runtime interpreter
5. Executes entry point
6. App accesses Morphic-API via syscalls

---

## Example: Hello World (Lua)

```lua
-- main.lua
local gfx = require("morphic.gfx")
local input = require("morphic.input")

gfx.clear(0x000000)
gfx.text(10, 10, "Hello Morphic!", 0xFFFFFF)
gfx.flip()

while true do
    local event = input.poll()
    if event and event.type == "key" and event.key == "q" then
        break
    end
end
```
