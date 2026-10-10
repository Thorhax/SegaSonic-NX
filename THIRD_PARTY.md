# Third-party components

| Component | Upstream | Licence | Use | Changes |
| --- | --- | --- | --- | --- |
| `src/core/zip.{h,cpp}` | Daytona-NX-Modern (`src/runtime/zip.*`, same author) | as that project | ZIP reader and inflate for the user's ROM set | Namespace only |
| `platform/switch/text.h` | Daytona-NX-Modern (`platform/switch/text.h`) | as that project | Menu text | None |
| MAME `src/mame/sega/segas32.cpp` (ROM_START sonic) | https://github.com/mamedev/mame (0.289) | BSD-3-Clause | Read: chip names, CRCs and region layout in `src/core/rom_set.cpp` | — |
| devkitPro, libnx, SDL2 (Switch) | https://devkitpro.org | ISC / zlib | Switch toolchain and app (Docker image `devkitpro-mesa-rust`) | — |
