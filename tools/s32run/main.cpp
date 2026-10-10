// s32run ROMS.zip: loads the SegaSonic set and reports each memory region
// (size and CRC-32). The headless runner the board will grow into: later
// steps add frames, input scripts, frame hashes and dumps for comparison
// against MAME.
#include "core/rom_set.h"
#include "core/zip.h"

#include <cstdio>
#include <exception>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: s32run ROMS.zip\n");
        return 2;
    }
    try {
        const segas32::RomImages img = segas32::load_sonic(argv[1]);
        auto show = [](const char *name, const std::vector<uint8_t> &r) {
            std::printf("  %-10s %8zu bytes  crc32 %08x\n", name, r.size(), segas32::crc32(r.data(), r.size()));
        };
        std::printf("s32run: %s: all 18 chips present, CRCs match MAME's sonic (rev. C)\n", argv[1]);
        show("main_cpu", img.main_cpu);
        show("sound_cpu", img.sound_cpu);
        show("tiles", img.tiles);
        show("sprites", img.sprites);
        // The V60 (24-bit bus) starts at 0xfffff0; System 32 maps the first
        // 1 MB of the program region again at 0xf00000, so that is ROM 0x0ffff0.
        const auto &m = img.main_cpu;
        std::printf("  V60 reset code (0xfffff0 = ROM 0x0ffff0):");
        for (int i = 0; i < 16; ++i) std::printf(" %02x", m[0x0ffff0 + i]);
        std::printf("\n");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "s32run: %s\n", e.what());
        return 1;
    }
    return 0;
}
