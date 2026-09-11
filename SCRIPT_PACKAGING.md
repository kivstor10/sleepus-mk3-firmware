# Sleepus MK3 Lua firmware packaging

The final 256 KiB of internal flash is split between the Lua archive and
persistent settings:

- Application: `0x08000000` through `0x080BFFFF`
- Lua archive: `0x080C0000` through `0x080FDFFF` (248 KiB)
- Settings slot A: `0x080FE000` through `0x080FEFFF`
- Settings slot B: `0x080FF000` through `0x080FFFFF`

The linker limits application output to 768 KiB. The packaging and verification
tools reject application overlap with the archive and archive overlap with the
two settings slots. Combined firmware HEX files intentionally contain no data
in the settings slots.

## Build a combined Intel HEX

```powershell
.\tools\build-lua-firmware.ps1
```

This compiles the AT32 application and Lua 5.4.8 runtime, then packages
`scripts/default.lua` into `build/Sleepus-MK3-Latest.hex`. Future builds
overwrite this stable path so there is only one file to select for upload.
To select another script or output file:

```powershell
.\tools\build-lua-firmware.ps1 `
  -Script scripts/my-script.lua `
  -Output build/Sleepus-MK3-custom.hex
```

The resulting HEX is sparse: it contains the application at `0x08000000` and
the archive at `0x080C0000`. Flash erased addresses between them remain `0xFF`.

## Archive format version 1

All integers are unsigned little-endian.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII magic `SLUA` |
| 4 | 2 | Format version (`1`) |
| 6 | 2 | Header size (`32`) |
| 8 | 4 | Total archive length |
| 12 | 4 | Lua source length |
| 16 | 4 | CRC-32 of Lua source |
| 20 | 4 | CRC-32 of the complete archive with this field set to zero |
| 24 | 4 | Flags, currently zero |
| 28 | 4 | Reserved, currently zero |
| 32 | variable | UTF-8 Lua source |

CRC-32 uses polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR
`0xFFFFFFFF`.

## Runtime behavior

The firmware validates the archive and initializes Lua before USB attachment.
For each physical controller input report, `on_input()` runs after the native
mod engine and its command buffer is applied last. A malformed archive, load
failure, timeout, instruction-budget failure, or runtime error disables Lua
and preserves native controller passthrough.
