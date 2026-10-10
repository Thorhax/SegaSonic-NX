# SegaSonic the Hedgehog for Nintendo Switch

A standalone Nintendo Switch port of SegaSonic the Hedgehog (Sega, 1992,
System 32 with three trackballs). Work in progress: see
`PROJECT_STATUS.md`.

No game data is in this repository or in any release: supply your own MAME
`sonic.zip` (SegaSonic the Hedgehog, Japan, rev. C) and put it in
`sdmc:/switch/sonic/` next to `sonic.nro`.

Host build (the core and the `s32run` test runner):

    cmake -S . -B build && cmake --build build -j
    ./build/s32run roms/sonic.zip

Switch build (devkitPro, or Docker with `devkitpro-mesa-rust:latest`):

    bash scripts/build_switch.sh    # -> ../sonic.nro
