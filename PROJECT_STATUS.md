# SegaSonic the Hedgehog (System 32) — Switch port status

## Plan

1. **Project and Switch shell** — done (2026-10-10).
2. MAME reference build with only the System 32 driver (frame/audio oracle).
3. Board on the host: V60 (MAME's core ported, interpreted), Z80, memory
   map, interrupts, I/O chip, protection; boot to the game's own screens.
4. Video (System 32 tilemaps with rotation/zoom, sprites, mixer), compared
   with MAME frame by frame.
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
