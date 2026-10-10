# SegaSonic the Hedgehog (System 32) — Switch port status

## Plan

1. **Project and Switch shell** — done (2026-10-10).
2. **MAME reference** — done (2026-10-10).
3. **Board on the host** — main CPU side done (2026-10-10): V60, memory map,
   interrupts, timers, I/O chip, EEPROM, trackball counters, protection.
   The sound Z80 comes with step 5.
4. **Video** — done on the host (2026-10-10): all layers, sprites, mixer.
5. **Sound** — done on the host (2026-10-11): Z80, 2x YM3438, RF5C68.
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
- Speed: tilemap pages (512x256 per page and bank) and the text layer are
  cached and redrawn where video RAM changed (as MAME's tilemap dirty
  marking). Host: 7.3 -> 3.8 ms per frame with a render every frame; the
  mixer is now ~40% of the time. Pixels re-checked: still identical.
- `s32run --inputs FILE` (lines "A-B key=value": coin1..3, start1..3,
  button1..3, trackx1..3, tracky1..3); `scripts/inputs/start_game.txt`
  coins up and starts: player select, the controls tutorial and the story
  intro play.

## Switch build 2 (2026-10-10, testing)

The board runs on the Switch: picture, no sound. Controls per player (up to
three controllers): left stick or D-pad = trackball (up to 40 counts a
frame; the game ignores per-frame deltas of 0x60 or more; MAME's X is
reversed, Y counts downwards), A/B = button, Plus = start, Minus = coin.
Menu: test switch toggle. EEPROM saved to sdmc:/switch/sonic/eeprom.bin on
menu and exit. Perf log: emu_ms and video_ms per frame.

### Switch build 2 result (2026-10-11)

Smooth, no graphical errors. Log: 58.4-60 fps, emu 5.4 ms mean (7.0 max),
video 5.1 ms mean (8.4 max), worst frame 13.4 ms of 16.7.

## Step 5: sound (2026-10-11)

- `src/cpu/z80`: MAME's Z80 (generated core) behind the shim.
- `src/board/scheduler`: MAME's scheduler copied (see its header). Needed
  once two CPUs share RAM: timeslices of at most 1/60 s, CPUs in order (V60,
  Z80), a CPU stopping early pulls the target back, abort when a timer
  becomes the head, input lines through a queue + zero-delay timer, a CPU
  held in reset eats its cycles, and inside a timer callback time() is the
  timer's expiry (this last one put the Z80's YM timer IRQ one instruction
  late until fixed). MAME's sound manager timer (every 1/50 s) is modelled
  too: it splits timeslices.
- Sound board: Z80 map (ROM, 8 KB bank of the 4 MB sound region, RF5C68
  window, shared RAM), ports (two YM3438 at 0x80/0x90, banks, sound
  interrupt controller at 0xc0/0xd0, 0xf1), Z80 held in reset by CNT2 of
  the I/O chip until the V60 releases it. YM3438: MAME's ymfm glue (timers
  as scheduler timers, busy end, mode-write and interrupt syncs). RF5C68:
  MAME's. Mixed to 48 kHz with MAME's route gains (0.30, 0.30, 0.40).
- Checked: Z80 trace, first 300 frames: 4,040,780 MAME trace lines, all
  identical (PC). V60 trace (60 frames) and 20,000 attract frames still
  identical. A played game (`scripts/inputs/play_sound.txt`: coin, start,
  button presses; MAME driven by `S32_INPUTS` in drive.lua): 3,600 of 3,600
  frames identical. Audio vs MAME's -wavwrite over that game: per-second RMS
  within ~1%, correlation 0.991-0.998 at a constant 6-sample lag (MAME's
  resampler).
- Host cost with sound (played game, render every frame): ~4.0 ms a frame;
  the mixer 27%, sprites 7%, ymfm ~10%, Z80 ~5%.

## Switch build 3 (2026-10-11, testing)

Sound on the Switch: `platform/switch/audio.h` plays the board's 48 kHz
stereo through an SDL callback from a ring buffer held near 60 ms (one
sample in 256 dropped or repeated when it drifts; callback on core 2).
Video drawn on core 1 (`render_worker.h`): at VBLANK start the emulation
thread copies the video state (`Video32::take_snapshot`: video RAM, palette,
mixer, latched sprite registers, the displayed sprite buffer) and a second
Video32 renders it (`render_snapshot`, which marks cached tiles dirty by
diffing video RAM against its previous snapshot) while the next frame
emulates; one frame of display latency. Host check: render_snapshot on a
second instance = the board's own render on 3,600 of 3,600 frames of a
played game. Menu: SOUND ON/MUTED. Perf log adds audio_q_ms; video_ms is
the worker's render time.
