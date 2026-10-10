# SegaSonic the Hedgehog (System 32) — Switch port status

## Plan

1. **Project and Switch shell** — done (2026-10-10).
2. **MAME reference** — done (2026-10-10).
3. **Board on the host** — main CPU side done (2026-10-10): V60, memory map,
   interrupts, timers, I/O chip, EEPROM, trackball counters, protection.
   The sound Z80 comes with step 5.
4. **Video** — done on the host (2026-10-10): all layers, sprites, mixer.
5. Sound: two YM3438 (ymfm) and the RF5C68.
6. Switch: trackball controls, perf, hardware tests.

## Hardware (MAME 0.289, segas32.cpp)

- Main: NEC V60 at 16.108 MHz (32.2159 MHz crystal / 2), 24-bit bus. Reset PC 0xfffff0;
  ROM 0x000000-0x1fffff, and its first 1 MB again at 0xf00000, so the reset
  code is ROM 0x0ffff0.
- Memory map: work RAM 0x200000 (64 KB, mirrored), video RAM 0x300000,
  sprite RAM 0x400000, sprite control 0x500000, palette 0x600000, mixer
  0x610000, shared sound RAM 0x700000, 315-5296 I/O 0xc00000 (trackballs
  through uPD4701s on the trackball board), interrupt control 0xd00000,
  random number 0xd80000.
- Sound: Z80 and two YM3438 at 8.054 MHz (crystal / 4), RF5C68 at 12.5 MHz
  (50 MHz / 4); its samples are in the sound region.
- Screen: 60 Hz, 224 lines, 320 or 416 pixels wide (game-selected).
- Protection (rev. C): MAME installs a write handler at 0x20e5c4 that sets
  the next level from a table in the program ROM (`sonic_level_load_protection`,
  segas32_m.cpp). The prototype set needs none.

## Step 1 (2026-10-10)

- New tree `Model2/segasonic-arcade/sonic-nx`; rules as the Model 2 ports.
- `src/core/rom_set`: loads `sonic.zip`, matches chips by name (any folder,
  any case) or CRC, checks CRCs, and builds MAME's four regions with the
  driver's layout (_x2/_x4 reloads, LOAD16_BYTE, LOAD64_WORD). The user's
  set: all 18 chips match rev. C.
- `tools/s32run`: reports the regions; will become the headless runner
  (frames, input scripts, hashes) used against MAME.
- Switch app (`platform/switch`): menu (start/resume, reset, perf overlay,
  exit), up to three controllers, ROM check with a clear error naming bad
  or missing chips, 4:3 presentation, 60 Hz deadline pacing, perf log
  `sdmc:/switch/sonic/sonic-perf.log`. Until the board exists, START GAME
  shows a test screen (colour bars; one marker per player on the left
  stick, white while A or B is held). Icon is a placeholder.

### Step 1 hardware test (2026-10-10)

Menu, ROM check and test screen work; 60.00 fps at 0.82 ms per frame; P1's
marker follows the stick. (The first log line, 0.22 fps, counted the time in
the menu; fixed.)

## Step 2: MAME reference (2026-10-10)

`Model2/mame-ref/sonic`: MAME 0.289 built with only segas32.cpp:

```sh
make SUBTARGET=sonic SOURCES=src/mame/sega/segas32.cpp USE_QTDEBUG=0 \
     NO_USE_PORTAUDIO=1 NO_USE_PIPEWIRE=1 -j
```
The generated `build/projects/sdl/mamesonic/gmake-linux/sonic.make` lacks
fontconfig/freetype on this machine (no libfontconfig-dev): append
`-l:libfontconfig.so.1 -lfreetype` to its LIBS lines and rerun make.

`tools/oracle/drive.lua` runs it headless and writes, per frame, the V60 PC
and a work-RAM checksum, plus snapshots and raw work-RAM dumps every N
frames (outputs stay local: game-derived):

```sh
S32_FRAMES=1800 S32_SNAP=150 S32_DUMP=dump S32_LOG=frames.log \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy mame-ref/sonic sonic \
  -rompath segasonic-arcade -video none -sound none -nothrottle \
  -skip_gameinfo -autoboot_script tools/oracle/drive.lua \
  -snapshot_directory snap -nvram_directory nv -cfg_directory cfg
```
1,800 frames run at ~150% speed on this host; the attract shows the SEGA
logo, the title and the TOP 15 table. Screen 416x224.

## Step 3: the board (2026-10-10)

- `src/cpu/v60`: MAME's V60 behind `mame_shim.h` (see THIRD_PARTY.md).
- `src/board/system32`: the main board (MAME's segas32 register behaviour),
  no video output or sound yet.
- Timing follows MAME's scheduler, so interrupts land on the same
  instruction: event times in attoseconds (MAME's own arithmetic:
  HZ_TO_ATTOSECONDS, from_ticks), a CPU slice of floor(delta / cycle)
  clocks, 8 clocks per instruction, and MAME's abort_timeslice when the CPU
  sets a timer due inside its slice. Time 0 is VBLANK start (MAME's screen
  origin): the first VBLANK interrupt is one frame in; VBLANK ends 38 lines
  after it starts. A frame is reported at VBLANK start before the interrupt
  is delivered, as MAME's frame_done sees it.
- Things MAME does that the game sees: the 93C46's write/erase busy times
  (the boot polls DO); each uPD4701 X counter starts at 0xfff (MAME's first
  frame update reads the reversed X port as 0xfff against 0); registers
  start zeroed.
- Checked: `tools/oracle/trace.lua` (MAME, -debug -debugger none) against
  `s32run --trace`, compared with `tools/oracle/trdiff.py`: all 2,047,052
  trace lines of the first 60 frames identical (r0-r2, SP, PC). Per-frame
  PC and work-RAM checksum (drive.lua vs `s32run --log`): 1,800 of 1,800
  frames identical, through the SEGA logo, title and TOP 15 attract.
- Host speed: 1,800 frames in ~3 s (~600 fps) on this PC.

## Step 4: video (2026-10-10)

- `src/board/video32`: MAME's System 32 video for one screen (see
  THIRD_PARTY.md). Rendered at VBLANK start like MAME's screen update;
  sprites drawn into the back buffer 50 us after VBLANK end
  (update_sprites), as MAME.
- Board start-up state from segas32_v.cpp device_start: video RAM
  0x1ff00 = 0x8000, mixer registers 0xffff (trace and RAM still identical).
- Checked against MAME snapshots (`s32run --frames DIR --every N` vs the
  oracle's PNGs, scratch `pngcmp.py`): 13 frames over 1,800 and 21 frames
  over 20,000 (both 416- and 320-wide modes, SEGA logo, title, TOP 15,
  gameplay demo with sprites): 0 pixels differ in all of them. 20,000
  frames of PC + work-RAM checksums identical too.
