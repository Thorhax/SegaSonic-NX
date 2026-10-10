# Third-party components

| Component | Upstream | Licence | Use | Changes |
| --- | --- | --- | --- | --- |
| `src/core/zip.{h,cpp}` | Daytona-NX-Modern (`src/runtime/zip.*`, same author) | as that project | ZIP reader and inflate for the user's ROM set | Namespace only |
| `platform/switch/text.h` | Daytona-NX-Modern (`platform/switch/text.h`) | as that project | Menu text | None |
| MAME `src/mame/sega/segas32.cpp` (ROM_START sonic) | https://github.com/mamedev/mame (0.289) | BSD-3-Clause | Read: chip names, CRCs and region layout in `src/core/rom_set.cpp` | — |
| MAME `src/devices/cpu/v60/` (v60.cpp, v60.h, *.hxx) | https://github.com/mamedev/mame (0.289) | BSD-3-Clause | **Code transplanted** into `src/cpu/v60/`: the V60 interpreter, verbatim apart from lines marked "Port:" | Device framework removed (bus through `mame_shim.h`'s `address_space`, constructor instead of device_start/state registration, `run(cycles)` instead of execute_run); registers start zeroed as MAME's devices do; per-instruction trace hook; `cycles_left()` for the board clock. The first commit is upstream unmodified. |
| MAME `src/mame/sega/segas32.cpp`, `segas32_v.cpp`, `segas32_m.cpp`, `315_5296.cpp`, `src/devices/machine/eepromser.cpp`, `eeprom.cpp`, `upd4701.cpp`, `src/emu/screen.cpp`, `diexec.cpp`, `schedule.cpp` | same | BSD-3-Clause | **Behaviour transplanted** into `src/board/system32.cpp`: memory map, interrupt controller and timers, VBLANK timing, 315-5296 I/O chip, 93C46 EEPROM state machine and busy times, uPD4701 counters, sprite-control and palette/mixer register side, SegaSonic's protection handler, and the scheduler rules that decide which instruction an interrupt lands on | Rewritten as one board class; no device framework |
| devkitPro, libnx, SDL2 (Switch) | https://devkitpro.org | ISC / zlib | Switch toolchain and app (Docker image `devkitpro-mesa-rust`) | — |
