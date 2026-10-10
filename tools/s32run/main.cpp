// s32run ROMS.zip [FRAMES] [--log FILE] [--dump DIR --every N]
// Runs the board headless. --log writes, per frame, "frame PC wram_sum" in
// the format of tools/oracle/drive.lua, for comparison with MAME.
#include "board/system32.h"
#include "core/rom_set.h"
#include "core/zip.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: s32run ROMS.zip [FRAMES] [--log FILE] [--dump DIR --every N]\n");
        return 2;
    }
    int frames = 0, every = 0;
    std::string log_path, dump_dir, trace_path, frames_dir; // --frames DIR: raw RGB frames every --every
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) every = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) trace_path = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames_dir = argv[++i];
        else frames = std::atoi(argv[i]);
    }
    try {
        segas32::RomImages img = segas32::load_sonic(argv[1]);
        std::printf("s32run: %s: all 18 chips present, CRCs match MAME's sonic (rev. C)\n", argv[1]);
        segas32::System32 board(std::move(img));
        FILE *log = log_path.empty() ? nullptr : std::fopen(log_path.c_str(), "w");
        FILE *trace = trace_path.empty() ? nullptr : std::fopen(trace_path.c_str(), "w");
        if (trace)
            board.cpu().trace = [trace](v60_device &c) {
                // as MAME's: trace FILE,cpu,,{tracelog "%08X %08X %08X %08X ",r0,r1,r2,sp}
                std::fprintf(trace, "%08X %08X %08X %08X %X\n", c.reg(0), c.reg(1), c.reg(2), c.reg(31), c.pc());
            };
        segas32::Inputs in;
        for (int f = 0; f < frames; ++f) {
            board.run_frame(in);
            const auto &w = board.work_ram();
            uint32_t sum = 0;
            for (size_t a = 0; a < w.size(); a += 4)
                sum = sum * 31 + (uint32_t(w[a]) | uint32_t(w[a + 1]) << 8 | uint32_t(w[a + 2]) << 16 | uint32_t(w[a + 3]) << 24);
            if (log) std::fprintf(log, "%d %08x %08x\n", f, board.cpu().pc(), sum);
            if (!frames_dir.empty() && every && f % every == 0) {
                const int w = board.render();
                char path[512];
                std::snprintf(path, sizeof path, "%s/frame_%05d_%dx224.rgb", frames_dir.c_str(), f, w);
                if (FILE *d = std::fopen(path, "wb")) {
                    const auto &px = board.frame_pixels();
                    for (int i = 0; i < w * 224; ++i) {
                        const uint8_t rgb[3] = {uint8_t(px[size_t(i)] >> 16), uint8_t(px[size_t(i)] >> 8), uint8_t(px[size_t(i)])};
                        std::fwrite(rgb, 1, 3, d);
                    }
                    std::fclose(d);
                }
            }
            if (!dump_dir.empty() && every && f % every == 0) {
                char path[512];
                std::snprintf(path, sizeof path, "%s/wram_%05d.bin", dump_dir.c_str(), f);
                if (FILE *d = std::fopen(path, "wb")) {
                    std::fwrite(w.data(), 1, w.size(), d);
                    std::fclose(d);
                }
            }
        }
        if (log) std::fclose(log);
        if (trace) std::fclose(trace);
        std::printf("s32run: %d frames, V60 at %08x\n", frames, board.cpu().pc());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "s32run: %s\n", e.what());
        return 1;
    }
    return 0;
}
