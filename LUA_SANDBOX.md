# Sleepus MK3 Lua sandbox

The implementation lives in `project/src/lua_sandbox.c`,
`project/src/lua_storage.c`, and `project/src/lua_sandbox_at32.c`. Lua receives
a copy of the current controller state and writes only to native command
buffers. USB reports, peripheral registers, and flash addresses are never
exposed to scripts.

## Lua configuration

Lua 5.4.8 is vendored under `libraries/lua`. The firmware build includes only
the core plus base, table, string, and math libraries; it does not compile the
standalone `lua.c` or `luac.c` programs.

Lua's `luaconf.h` enables its source-level 32-bit configuration:

```c
#define LUA_32BITS 1
```

Keep the existing compiler and linker options:

```text
-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
```

`LUA_FLOAT_FLOAT` makes `lua_Number` a 32-bit IEEE-754 `float`, and
`LUA_INT_INT` makes `lua_Integer` a 32-bit signed `int`. The AT32F435 M4F can
execute single-precision add, subtract, multiply, divide, conversion, and
comparison in its FPv4-SP-D16 FPU. Transcendental functions such as `sin` and
`pow` still use the C math library and are not single-instruction FPU
operations.

All Lua translation units and all C files that exchange Lua numeric values
must use the same `luaconf.h`. `lua_sandbox.c` contains compile-time size
checks and will fail to build if either configured type is not 32-bit.

Add `-lm` to the final link when compiling `lmathlib.c`. The linker script's
current `libm.a (*)` discard rule must also be removed when Lua is added.
Ordinary Lua `+`, `-`, `*`, `/`, comparisons, and conversions use FPv4-SP-D16;
math-library calls such as `sinf` still execute library routines.

## Libraries and allocator

The sandbox opens only base, table, string, and math. It never opens `io`,
`os`, `debug`, `package`, or coroutine. `load`, `loadfile`, `dofile`, and
`collectgarbage` are removed. Scripts are loaded in text-only mode.

Use the included fixed arena rather than the firmware's general heap. A failed
allocation returns null and Lua converts it to a protected out-of-memory error:

```c
#include "lua_arena.h"

static uint64_t lua_memory[128U * 1024U / sizeof(uint64_t)];
static lua_arena_t lua_arena;

lua_arena_init(&lua_arena, lua_memory, sizeof(lua_memory));
lua_sandbox_init(lua_arena_alloc, &lua_arena, lua_sandbox_at32_port());
```

## Main-loop integration

`lua_runtime_init()` initializes the arena, validates both archive CRCs, and
loads the script after board hardware initialization. Invalid archives and
load errors leave Lua inactive while native passthrough continues.

The runtime uses this event budget:

```c
static lua_sandbox_budget_t script_budget =
{
  4000U, /* maximum VM instructions per event */
  1U,    /* wall-clock deadline in milliseconds */
  100U   /* check every 100 VM instructions */
};

```

Use this order after receiving a fresh physical controller report:

```c
current_time = get_system_tick();
lua_sandbox_begin_frame(&cached_data, current_time);
lua_sandbox_task(current_time);

if(!lua_sandbox_run_event("on_input", &script_budget,
                          error, sizeof(error)))
{
  /* Log the error and continue with transparent passthrough. */
}

output_data = cached_data;
lua_sandbox_apply(&output_data);
```

`lua_runtime_task()` also runs `on_input` from the main loop at most once per
10 milliseconds using the latest cached controller snapshot. This keeps the
chassis buttons and OLED responsive while the physical controller is idle.
It does not run from a USB ISR or synthesize an upstream report before a real
controller input report has been cached. The initial frame runs before USB
startup so the Lua UI is already in the framebuffer when the OLED is enabled.
The runtime tolerates two consecutive protected-event failures and disables
Lua on the third; a successful frame resets the failure counter.

Never call Lua from a USB ISR. The USB path must continue using the previous
validated report if the policy task does not finish before its deadline.
Call `lua_sandbox_storage_task()` only from a low-priority maintenance path.

## Lua API

Controller values use `-100..100` for sticks and `0..100` for buttons and
triggers. Native bindings clamp all writes. IDs are exported as fields such as
`controller.A`, `controller.RT`, and `controller.LX`.

```lua
function on_input()
  local recoil = storage.read("recoil") or 12.5

  if controller.get_val(controller.RT) > 80 then
    controller.set_val(controller.RY,
      controller.get_val(controller.RY) + recoil)
  end
end
```

`controller.get_ptime(id)` returns milliseconds since the physical input value
last changed. Script-generated values do not reset this timer.

Controller ID slots 14 and 15 are intentionally reserved rather than exported
as GUIDE or SHARE. Their report semantics must be confirmed from an MK3
controller capture before adding public IDs.

`controller.block_inputs()` clears the native output snapshot for the current
frame before scripted and macro overrides are applied.

## Chassis buttons

The four active-low OLED navigation buttons are exposed through a separate
module:

```lua
if device.get_val(device.BTN_UP) == 100 then
  display.clear()
  display.draw_text(0, 0, "UP")
end
```

Available IDs are `device.BTN_UP`, `device.BTN_DOWN`, `device.BTN_SELECT`, and
`device.BTN_BACK`. `device.get_val(id)` returns `100` while pressed and `0`
while released. These values come from a dedicated debounced hardware
snapshot and are never inserted into the USB `controller_data_t` report.
`device.config_request(1)` starts export, `device.config_request(2)` starts
import, and `device.config_status()` returns the asynchronous operation state.

The packaged UI maps the physical UP and DOWN buttons to previous and next
field. SELECT decreases the current value and BACK increases it. Holding a
navigation button repeats after 350 milliseconds at an 80 millisecond rate;
numeric settings and the operator list use the same repeat timing, while
binary settings change only once per press. The game selector is the menu root:
SELECT or BACK switches between R6, RUST, and CONFIG. DOWN opens the existing
R6 side and operator settings, while RUST opens a `COMING SOON` screen and
cannot enter those settings. CONFIG offers EXPORT and IMPORT. UP returns from
each child screen toward game selection.
Operator selection stops at each end instead of wrapping.

The packaged recoil engine applies the configured vertical and horizontal
values at rest. Outside the configured dead zone, compensation is multiplied
by the movement percentage and fades smoothly toward zero as right-stick
deflection approaches 100 percent. An axis with zero configured compensation
is not overridden, preserving the corresponding physical stick value exactly.
Vertical, horizontal, movement, dead-zone, and rapid-fire values are held in
separate RAM profiles for every side, operator, and primary/secondary weapon
combination. Profiles are loaded lazily from flash, updated in RAM while the
menu is open, and committed only when the user advances past `READY`.

## Asynchronous macros

The compact schema applies one controller value for each step's `wait`
duration:

```lua
local accepted, reason = controller.submit_macro({
  {btn = controller.B, val = 100, wait = 60},
  {btn = controller.B, val = 0,   wait = 40}
})
```

`btn` is any supported controller ID, `val` is clamped to `-100..100`, and
`wait` must be `1..60000` milliseconds. The older
`{duration_ms=..., values={...}}` multi-value step remains supported.

Lua submits the complete table once. C validates and copies it into a fixed
queue, so the scheduler retains no Lua references and performs no allocations.
Limits are four queued macros, 32 steps per macro, 12 values per legacy step,
and 60 seconds per step. `lua_sandbox_task()` advances steps from timestamps
in the main loop; it never sleeps and is never called from the 1 kHz USB ISR.
Macro output has final precedence over ordinary script output.

`controller.submit_macro()` returns `true` when accepted. If all four queued
slots are occupied, it returns `false, "macro queue full"`; it does not ignore
submissions merely because another macro is active. Scripts that repeat a
macro while a control is held must pace submissions by the macro duration to
avoid building a backlog.

`controller.cancel_macros()` immediately releases the active macro, discards
all queued macros, and clears current macro overrides. It does not clear the
physical controller snapshot or ordinary script overrides. The packaged
`scripts/default.lua` uses this operation when session settings are opened.

Independent GPC-style combos use one of eight fixed named channels:

```lua
controller.start_macro("crouch", crouch_steps)
controller.macro_running("crouch")
controller.stop_macro("crouch")
```

Names contain 1 to 15 bytes. `start_macro()` returns `false` when that name is
already running, or `false, "named macro channels full"` when all channels are
occupied. `stop_macro()` returns whether it found the name. Named macros run
concurrently and merge in channel order; a later channel wins if two active
steps override the same controller ID. Like queued macros, all definitions are
validated and copied into fixed C storage before execution.

## Display and status LED

`display.clear()`, `display.draw_text(x, y, text)`, and
`display.draw_pixel(x, y, state)` update the existing 128 by 32 framebuffer.
I2C transfer remains incremental in `hardware_task()`. Text uses a 5 by 7
printable ASCII font for space through uppercase `Z`; lowercase letters are
rendered as uppercase and unsupported characters are blank.

`led.set(true)` and `led.set(false)` control the existing PC15 status LED
through `set_status_led()`. This is the board's only LED interface.

## Persistent storage

The linker reserves `0x080C0000` through `0x080FDFFF` for the Lua archive and
two 4 KiB settings slots at `0x080FE000` and `0x080FF000`. Each slot spans two
2 KiB erase sectors on the AT32F435xG.
`tools/package-lua.js` rejects an archive that would overlap either slot; see
`SCRIPT_PACKAGING.md` for the archive format.

Storage supports nil/delete, boolean, 32-bit integer, single-precision float,
and strings up to 63 bytes. Keys are 1 to 31 bytes. Tables and userdata are
rejected.

`storage.write()` and `storage.write_loadout()` update RAM only.
`storage.commit()` requests one maintenance commit. The task erases and writes
the inactive slot with a new generation and CRC-32, verifies it, then makes it
active; the previous slot remains valid through interruption or corruption.
The settings are in flash bank 2 while application and USB interrupt code are
in bank 1. Flash maintenance runs from the main loop, never from a USB ISR.

`storage.read_loadout(side, operator, weapon)` returns vertical, horizontal,
movement, dead-zone, and rapid-fire values or nil for an unused record.
`storage.write_loadout()` accepts side `1..2`, operator `1..36`, weapon `1..2`,
and validates every setting before marking the image dirty.

The WebDFU updater currently mass-erases flash, so these settings will be lost
during firmware updates unless the updater and bootloader implement an
explicit preserve/restore operation.

## Removable config backup

CONFIG uses the female USB-A connector for either the controller or a mass
storage device, never both simultaneously. Start EXPORT or IMPORT, unplug the
controller, and insert a FAT12, FAT16, or FAT32 USB stick. The firmware does
not format media and does not support exFAT or long filenames.

The root backup is `SLEEPUS.CFG`, a fixed 4 KiB versioned image with CRC-32.
Export first writes and syncs `SLEEPUS.NEW`, rotates the previous file to
`SLEEPUS.BAK`, and then promotes the new file. Import tries CFG, NEW, and BAK
in that order. It accepts the current storage format plus the supported v3 and
v4 legacy formats, migrates a valid legacy image to the current format, and
waits for the A/B flash commit to verify before reporting completion. Corrupt
or unsupported images are rejected without modifying the active settings.

After every completed or failed file operation, the host is reinitialized for
the Xbox controller class. Remove the USB stick and reconnect the controller;
normal gated Xbox discovery and upstream enumeration then resume.

## Security invariants

- Accept script text only; reject Lua bytecode with `luaL_loadbufferx(...,"t")`.
- Keep all C bindings bounded and free of arbitrary addresses or light userdata
  returned to Lua.
- Treat malformed scripts as untrusted input and execute only through
  `lua_pcall` with instruction and wall-clock hooks.
- Disable the script after repeated failures and fall back to unchanged native
  controller passthrough.
- Read-protection and disabled production debug access protect firmware from
  external extraction; the Lua VM alone is not a formal isolation boundary
  against VM or binding vulnerabilities.