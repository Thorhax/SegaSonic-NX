# SegaSonic the Hedgehog for Nintendo Switch

A standalone Nintendo Switch port of SegaSonic the Hedgehog (Sega, 1992,
System 32 with three trackballs). The main CPU (V60), sound CPU (Z80), two
YM3438 FM chips, the RF5C68 PCM chip and the System 32 video hardware are
emulated after MAME, and checked against it frame by frame. Up to three
players, one controller each. Runs at 60 fps with sound.

No game data is in this repository or in any release: supply your own MAME
`sonic.zip` (SegaSonic the Hedgehog, Japan, rev. C).

## Installation

```text
sdmc:/switch/sonic/
├── sonic.nro
└── sonic.zip
```

1. Copy `sonic.nro` to `sdmc:/switch/sonic/` (or extract the release zip to
   the root of the SD card).
2. Put your MAME `sonic.zip` in the same folder. The app checks every chip
   and names any that are missing or bad.
3. Launch from the Homebrew Menu.

Settings and high scores are saved to `sdmc:/switch/sonic/eeprom.bin`.

## Controls

| Action | Input |
| :--- | :--- |
| Trackball | Left stick or D-pad |
| Roll left / right (changes character at player select) | `L` / `R` (hold to keep rolling) |
| Button (jump) | `A` or `B` |
| Start | `+` |
| Coin | `-` |
| Menu | `+` and `-` together |

At player select, the game changes character once the trackball has been
rolling for about a fifth of a second: hold the stick, or press `L` / `R`.

The menu has resume, reset, a performance overlay, the cabinet's test switch
(for the game's own test menu and settings), sound on/muted, and exit.

## Building

Fetch ymfm first (git-ignored, pinned to the copy in MAME 0.289):

    bash scripts/fetch_ymfm.sh

Host build (the core and the `s32run` test runner):

    cmake -S . -B build && cmake --build build -j
    ./build/s32run roms/sonic.zip

Switch build (devkitPro, or Docker with `devkitpro-mesa-rust:latest`):

    bash scripts/build_switch.sh    # -> ../sonic.nro

See `PROJECT_STATUS.md` for how the port was verified against MAME, and
`THIRD_PARTY.md` for the MAME code it builds on (BSD-3-Clause).
