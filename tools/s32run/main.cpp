// s32run ROMS.zip [FRAMES] [options]: runs the board headless.
//   --log FILE          per frame "frame PC wram_sum" (tools/oracle/drive.lua format)
//   --frames DIR        raw RGB frames every --every N frames
//   --dump DIR          raw work-RAM dumps every --every N frames
//   --trace FILE        V60 trace (r0 r1 r2 sp pc before each instruction)
//   --ztrace FILE       Z80 trace (af bc de hl pc before each instruction)
//   --wav FILE          the mixed audio, 16-bit stereo 48 kHz
//   --eeprom FILE       load the EEPROM from FILE if it exists, save it at the end
//   --inputs FILE       lines "A-B key=value" for frames A..B: coin1..3,
//                       start1..3, button1..3, test1, service1 (0/1), trackx1..3, tracky1..3
// Frames are logged at MAME's frame_done point (VBLANK start, before the
// interrupt reaches the CPU).
#include "board/system32.h"
#include "core/rom_set.h"
#include "core/zip.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: s32run ROMS.zip [FRAMES] [--log F] [--frames DIR] [--dump DIR] [--every N]\n"
                             "       [--trace F] [--ztrace F] [--wav F] [--inputs F] [--eeprom F]\n");
        return 2;
    }
    int frames = 0, every = 0;
    std::string log_path, dump_dir, trace_path, frames_dir, inputs_path, wav_path, ztrace_path, eeprom_path;
    for (int i = 2; i < argc; ++i) {
        auto arg = [&](const char *name) { return !std::strcmp(argv[i], name) && i + 1 < argc; };
        if (arg("--log")) log_path = argv[++i];
        else if (arg("--dump")) dump_dir = argv[++i];
        else if (arg("--every")) every = std::atoi(argv[++i]);
        else if (arg("--trace")) trace_path = argv[++i];
        else if (arg("--ztrace")) ztrace_path = argv[++i];
        else if (arg("--frames")) frames_dir = argv[++i];
        else if (arg("--inputs")) inputs_path = argv[++i];
        else if (arg("--wav")) wav_path = argv[++i];
        else if (arg("--eeprom")) eeprom_path = argv[++i];
        else frames = std::atoi(argv[i]);
    }
    try {
        segas32::RomImages img = segas32::load_sonic(argv[1]);
        std::printf("s32run: %s: all 18 chips present, CRCs match MAME's sonic (rev. C)\n", argv[1]);
        segas32::System32 board(std::move(img));
        if (!eeprom_path.empty())
            if (FILE *f = std::fopen(eeprom_path.c_str(), "rb")) {
                uint16_t saved[64];
                if (std::fread(saved, 2, 64, f) == 64) std::copy(saved, saved + 64, board.eeprom().data.begin());
                std::fclose(f);
            }

        struct Ev {
            int a, b;
            std::string key;
            int value;
        };
        std::vector<Ev> script;
        if (!inputs_path.empty()) {
            FILE *f = std::fopen(inputs_path.c_str(), "r");
            if (!f) throw std::runtime_error("cannot open " + inputs_path);
            char line[256];
            while (std::fgets(line, sizeof line, f)) {
                int a, b, v;
                char key[32];
                if (std::sscanf(line, "%d-%d %31[a-z0-9]=%d", &a, &b, key, &v) == 4) script.push_back({a, b, key, v});
            }
            std::fclose(f);
        }

        FILE *log = log_path.empty() ? nullptr : std::fopen(log_path.c_str(), "w");
        FILE *trace = trace_path.empty() ? nullptr : std::fopen(trace_path.c_str(), "w");
        FILE *ztrace = ztrace_path.empty() ? nullptr : std::fopen(ztrace_path.c_str(), "w");
        FILE *wav = wav_path.empty() ? nullptr : std::fopen(wav_path.c_str(), "wb");
        size_t wav_frames = 0;
        if (wav) std::fseek(wav, 44, SEEK_SET);
        if (trace)
            board.cpu().trace = [trace](v60_device &c) {
                std::fprintf(trace, "%08X %08X %08X %08X %X\n", c.reg(0), c.reg(1), c.reg(2), c.reg(31), c.pc());
            };
        if (ztrace)
            board.sound_cpu().trace = [ztrace](z80_device &z) {
                std::fprintf(ztrace, "%04X %04X %04X %04X %04X\n", z.af(), z.bc(), z.de(), z.hl(), z.pc());
            };

        int current = 0;
        board.on_frame = [&]() {
            const int f = current;
            const auto &w = board.work_ram();
            if (log) {
                uint32_t sum = 0;
                for (size_t a = 0; a < w.size(); a += 4)
                    sum = sum * 31 + (uint32_t(w[a]) | uint32_t(w[a + 1]) << 8 | uint32_t(w[a + 2]) << 16 | uint32_t(w[a + 3]) << 24);
                std::fprintf(log, "%d %08x %08x\n", f, board.cpu().pc(), sum);
            }
            if (!every || f % every != 0) return;
            char path[512];
            if (!frames_dir.empty()) {
                const int width = board.render();
                std::snprintf(path, sizeof path, "%s/frame_%05d_%dx224.rgb", frames_dir.c_str(), f, width);
                if (FILE *d = std::fopen(path, "wb")) {
                    const auto &px = board.frame_pixels();
                    for (int i = 0; i < width * 224; ++i) {
                        const uint8_t rgb[3] = {uint8_t(px[size_t(i)] >> 16), uint8_t(px[size_t(i)] >> 8), uint8_t(px[size_t(i)])};
                        std::fwrite(rgb, 1, 3, d);
                    }
                    std::fclose(d);
                }
            }
            if (!dump_dir.empty()) {
                std::snprintf(path, sizeof path, "%s/wram_%05d.bin", dump_dir.c_str(), f);
                if (FILE *d = std::fopen(path, "wb")) {
                    std::fwrite(w.data(), 1, w.size(), d);
                    std::fclose(d);
                }
            }
        };

        for (int f = 0; f < frames; ++f) {
            segas32::Inputs in;
            for (const Ev &e : script) {
                if (f < e.a || f > e.b) continue;
                const int p = e.key.back() - '1';
                if (p < 0 || p > 2) continue;
                const std::string k = e.key.substr(0, e.key.size() - 1);
                if (k == "coin") in.coin[p] = e.value;
                else if (k == "start") in.start[p] = e.value;
                else if (k == "button") in.button[p] = e.value;
                else if (k == "trackx") in.track_x[p] = int16_t(e.value);
                else if (k == "tracky") in.track_y[p] = int16_t(e.value);
                else if (k == "test") in.test = e.value != 0;
                else if (k == "service") in.service = e.value != 0;
            }
            current = f;
            board.run_frame(in);
            if (wav) {
                const auto &a = board.audio();
                std::fwrite(a.data(), 2, a.size(), wav);
                wav_frames += a.size() / 2;
            }
        }
        if (log) std::fclose(log);
        if (!eeprom_path.empty())
            if (FILE *f = std::fopen(eeprom_path.c_str(), "wb")) {
                std::fwrite(board.eeprom().data.data(), 2, 64, f);
                std::fclose(f);
            }
        if (trace) std::fclose(trace);
        if (ztrace) std::fclose(ztrace);
        if (wav) { // RIFF header: 16-bit stereo PCM
            const uint32_t rate = segas32::System32::kAudioRate, bytes = uint32_t(wav_frames * 4);
            auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, wav); };
            auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, wav); };
            std::fseek(wav, 0, SEEK_SET);
            std::fwrite("RIFF", 1, 4, wav);
            u32(36 + bytes);
            std::fwrite("WAVEfmt ", 1, 8, wav);
            u32(16), u16(1), u16(2), u32(rate), u32(rate * 4), u16(4), u16(16);
            std::fwrite("data", 1, 4, wav);
            u32(bytes);
            std::fclose(wav);
        }
        std::printf("s32run: %d frames, V60 at %08x, Z80 at %04x\n", frames, board.cpu().pc(), board.sound_cpu().pc());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "s32run: %s\n", e.what());
        return 1;
    }
    return 0;
}
