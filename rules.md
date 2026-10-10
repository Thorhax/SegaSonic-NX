# Project rules — SegaSonic the Hedgehog (System 32) for Nintendo Switch

These are binding.

## Git

1. **No `Co-Authored-By` trailer. Ever.** Commits are made under the user's
   name only, with no attribution of any kind to an assistant or tool.
2. **Never mention Claude, AI, or an assistant** in a commit message, branch
   name, PR body, tag, or any committed file. `CLAUDE.md` is git-ignored.
3. **Commit messages are short and plain.** Subject under ~70 characters, a
   few lines of body: what changed, the measurement behind it, anything that
   would bite the next person.
4. **Vendored repositories are git-ignored** (`extern/`): fetched and pinned,
   never committed.
5. **Build output is git-ignored** (`build/`, `build-*/`, `.nro`).

## Game data

6. **No ROM data, ever.** Nothing derived from the ROM bytes is committed or
   released: no ROM files, extracted regions, dumps, screenshots of frames
   used as test data, or NROs with a bundled ROM. The user supplies
   `sonic.zip` (MAME set `sonic`); the app reads it from
   `sdmc:/switch/sonic/`.
7. **Analysis evidence is committed; game-derived output is not.** Addresses,
   register meanings and notes from our own analysis go in
   `PROJECT_STATUS.md`; trace files and frame dumps stay local.

## Reference

8. **MAME is the reference** for what the hardware does (`src/mame/sega/
   segas32*.cpp`, `src/devices/cpu/v60/`, `src/devices/sound/rf5c68.cpp`).
   Code transplanted from MAME keeps its BSD-3-Clause notice and is listed in
   `THIRD_PARTY.md` with what was changed.
9. **Every behaviour change is checked against MAME** (frame and audio
   comparison on the host) before it reaches a hardware test.
