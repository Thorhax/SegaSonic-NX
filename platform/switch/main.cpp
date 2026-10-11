// Nintendo Switch app for the SegaSonic the Hedgehog (System 32) port.
// Menu, input, ROM check, 4:3 presentation, 60 Hz pacing and the perf log
// around the System 32 board (src/board). Video is drawn on core 1
// (render_worker.h), audio played from a ring buffer (audio.h).
#define SDL_MAIN_HANDLED
#include "audio.h"
#include "controls.h"
#include "render_worker.h"
#include "text.h"
#include "board/system32.h"
#include "core/rom_set.h"

#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

extern "C" void userAppInit(void) {
    romfsInit();
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/sonic", 0777);
    chdir("sdmc:/switch/sonic");
}

extern "C" void userAppExit(void) {
    romfsExit();
}

namespace {

constexpr int kDisplayWidth = 1280;
constexpr int kDisplayHeight = 720;
constexpr const char *kAppName = "SegaSonic the Hedgehog";
constexpr const char *kPerfLogName = "sonic-perf.log";
// System 32 video: 60 Hz, 224 visible lines, 320 or 416 pixels wide (the
// game picks; MAME's screen is 416). The texture is the wide mode.
constexpr int kScreenWidth = 416, kScreenHeight = 224;
constexpr uint64_t kFrameNs = 16'666'667ULL;

std::string find_rom_file() {
    static const char *candidates[] = {"sdmc:/switch/sonic/sonic.zip", "sonic.zip", "romfs:/sonic.zip"};
    for (const char *path : candidates) {
        struct stat st{};
        if (stat(path, &st) == 0 && st.st_size > 0) return path;
    }
    return "sonic.zip";
}

// Cabinet inputs from up to three controllers: the left stick (or D-pad)
// rolls that player's trackball, A or B is the button, Plus start, Minus coin.
// L / R roll the trackball left / right for at least kRollFrames: player
// select changes character only after about 12 frames of trackball motion.
constexpr int kRollFrames = 20;
constexpr float kRollSpeed = 0.5f;

segas32::Inputs map_inputs(const switch_app::Pad (&pads)[switch_app::Pads::kPlayers], int (&roll)[switch_app::Pads::kPlayers],
                           uint32_t (&previous)[switch_app::Pads::kPlayers]) {
    segas32::Inputs in;
    for (int p = 0; p < switch_app::Pads::kPlayers; ++p) {
        const auto &pad = pads[p];
        const uint32_t pressed = pad.buttons & ~previous[p];
        previous[p] = pad.buttons;
        if (pressed & switch_app::L) roll[p] = -kRollFrames;
        if (pressed & switch_app::R) roll[p] = kRollFrames;
        if (pad.buttons & switch_app::L) roll[p] = std::min(roll[p], -1);
        if (pad.buttons & switch_app::R) roll[p] = std::max(roll[p], 1);
        float dx = pad.lx, dy = pad.ly;
        if (pad.buttons & switch_app::Left) dx = -1.0f;
        if (pad.buttons & switch_app::Right) dx = 1.0f;
        if (pad.buttons & switch_app::Up) dy = 1.0f;
        if (pad.buttons & switch_app::Down) dy = -1.0f;
        if (roll[p] < 0) dx = -kRollSpeed, ++roll[p];
        else if (roll[p] > 0) dx = kRollSpeed, --roll[p];
        auto curve = [](float v) { // dead zone, then up to kMax counts a frame
            constexpr float kMax = 40.0f; // the game ignores deltas of 0x60 or more
            const float a = std::fabs(v);
            if (a < 0.12f) return 0.0f;
            return std::copysign((a - 0.12f) / 0.88f * kMax, v);
        };
        // MAME's TRACKX ports are reversed and Y counts downwards.
        in.track_x[p] = int16_t(std::lround(-curve(dx)));
        in.track_y[p] = int16_t(std::lround(-curve(dy)));
        in.button[p] = (pad.buttons & (switch_app::A | switch_app::B)) != 0;
        in.start[p] = (pad.buttons & switch_app::Plus) && !switch_app::menu_chord(pad.buttons);
        in.coin[p] = (pad.buttons & switch_app::Minus) && !switch_app::menu_chord(pad.buttons);
    }
    return in;
}

// What reached the board in one perf window, per player (sonic-perf.log).
struct InputStats {
    int x_frames = 0, y_frames = 0, x_max = 0, y_max = 0;
    uint32_t buttons = 0;
    float lx_max = 0.0f;
    void add(const segas32::Inputs &in, const switch_app::Pad &pad, int p) {
        if (in.track_x[p]) ++x_frames, x_max = std::max(x_max, std::abs(int(in.track_x[p])));
        if (in.track_y[p]) ++y_frames, y_max = std::max(y_max, std::abs(int(in.track_y[p])));
        buttons |= pad.buttons;
        lx_max = std::max(lx_max, std::fabs(pad.lx));
    }
};

} // namespace

int main(int, char **) {
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 0b0001);
    switch_app::Pads pads;
    pads.init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return 1;
    SDL_Window *window = SDL_CreateWindow(kAppName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          kDisplayWidth, kDisplayHeight, SDL_WINDOW_FULLSCREEN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (!renderer) {
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, kDisplayWidth, kDisplayHeight);
    SDL_Texture *screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                    kScreenWidth, kScreenHeight);
    SDL_SetTextureBlendMode(screen_texture, SDL_BLENDMODE_NONE);

    FILE *perf_log = std::fopen(kPerfLogName, "a");
    if (perf_log) {
        std::fprintf(perf_log, "\n=== %s SWITCH LOG START ===\n", kAppName);
        std::fflush(perf_log);
    }

    std::unique_ptr<segas32::System32> game;
    std::unique_ptr<switch_app::RenderWorker> worker;
    switch_app::Audio audio;
    audio.open();
    std::vector<uint32_t> pixels;
    int width = 416;
    bool muted = false;
    bool menu = true, running = true, wait_release = false, show_perf = false, test_switch = false;
    int selection = 0;
    uint32_t previous = 0;
    int roll[switch_app::Pads::kPlayers] = {};
    uint32_t game_previous[switch_app::Pads::kPlayers] = {};
    InputStats input_stats[switch_app::Pads::kPlayers];
    std::string status = "Place sonic.zip (MAME set sonic) in sdmc:/switch/sonic/ then choose START GAME.";
    const std::string rom_path = find_rom_file();

    uint64_t next_frame_ns = 0, window_start = armGetSystemTick(), frames = 0, busy = 0, emu = 0, render = 0;
    double fps = 0.0, busy_ms = 0.0, emu_ms = 0.0, render_ms = 0.0;
    int last_track_x = 0, last_track_y = 0;

    auto start_game = [&]() {
        try {
            worker.reset();
            game.reset();
            game = std::make_unique<segas32::System32>(segas32::load_sonic(rom_path));
            worker = std::make_unique<switch_app::RenderWorker>(game->roms());
            game->on_frame = [&] { worker->submit(*game); };
            pixels.clear();
            game->eeprom().data.fill(0xffff);
            uint16_t saved[64];
            if (FILE *f = std::fopen("eeprom.bin", "rb")) { // settings and high scores
                if (std::fread(saved, 2, 64, f) == 64) std::copy(saved, saved + 64, game->eeprom().data.begin());
                std::fclose(f);
            }
            status = "LEFT STICK: TRACKBALL  L/R: ROLL (PLAYER SELECT)  A/B: BUTTON  PLUS: START  MINUS: COIN.";
            return true;
        } catch (const std::exception &e) {
            status = std::string("Error: ") + e.what();
            worker.reset();
            game.reset();
            return false;
        }
    };

    auto save_eeprom = [&]() {
        if (!game) return;
        if (FILE *f = std::fopen("eeprom.bin.tmp", "wb")) {
            const bool ok = std::fwrite(game->eeprom().data.data(), 2, 64, f) == 64;
            std::fclose(f);
            if (ok) std::rename("eeprom.bin.tmp", "eeprom.bin");
        }
    };

    while (running && appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT) running = false;

        switch_app::Pad pad[switch_app::Pads::kPlayers];
        for (int p = 0; p < switch_app::Pads::kPlayers; ++p) pad[p] = pads.read(p);
        const uint32_t pressed = pad[0].buttons & ~previous;
        previous = pad[0].buttons;
        if (wait_release && !pad[0].buttons) wait_release = false;

        if (!menu && !wait_release && switch_app::menu_chord(pad[0].buttons)) {
            menu = true;
            wait_release = true;
            audio.pause();
            save_eeprom();
        }

        if (menu) {
            constexpr int kItems = 6;
            if (!wait_release) {
                if (pressed & switch_app::Up) selection = (selection + kItems - 1) % kItems;
                if (pressed & switch_app::Down) selection = (selection + 1) % kItems;
                if ((pressed & switch_app::B) && game) {
                    menu = false;
                    wait_release = true;
                } else if (pressed & switch_app::A) {
                    switch (selection) {
                    case 0:
                        if (game || start_game()) menu = false, wait_release = true;
                        break;
                    case 1:
                        if (start_game()) menu = false, wait_release = true;
                        break;
                    case 2:
                        show_perf = !show_perf;
                        break;
                    case 3:
                        test_switch = !test_switch;
                        status = test_switch ? "Test switch held: resume to enter the test menu." : "Test switch released.";
                        break;
                    case 4:
                        muted = !muted;
                        audio.mute(muted);
                        break;
                    case 5:
                        save_eeprom();
                        running = false;
                        break;
                    }
                }
            }
            SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 240, 240, 245, 255);
            switch_app::text(renderer, "SEGASONIC THE HEDGEHOG - NINTENDO SWITCH", 40, 30, 3, 40, 1);
            const std::string labels[kItems] = {
                game ? "RESUME GAME" : "START GAME", "RESET GAME",
                show_perf ? "PERFORMANCE OVERLAY: [ON]" : "PERFORMANCE OVERLAY: [OFF]",
                test_switch ? "TEST SWITCH: [ON - HOLDING]" : "TEST SWITCH: [OFF]",
                muted ? "SOUND: MUTED" : "SOUND: ON", "EXIT TO HOMEBREW MENU"};
            for (int i = 0; i < kItems; ++i) {
                if (i == selection) SDL_SetRenderDrawColor(renderer, 255, 205, 50, 255);
                else SDL_SetRenderDrawColor(renderer, 210, 210, 220, 255);
                switch_app::text(renderer, (i == selection ? "> " : "  ") + labels[i], 60, 90 + i * 35, 2, 60, 1);
            }
            SDL_SetRenderDrawColor(renderer, 180, 180, 190, 255);
            switch_app::text(renderer, status, 40, 300, 2, 70, 3);
            SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
            switch_app::text(renderer, "PLAYERS 1-3: ONE CONTROLLER EACH. LEFT STICK OR D-PAD: TRACKBALL. A/B: BUTTON", 40, 560, 2, 70, 1);
            switch_app::text(renderer, "L/R: ROLL LEFT/RIGHT (CHANGES CHARACTER)   PLUS=START   MINUS=COIN   PLUS+MINUS=MENU", 40, 590, 2, 70, 1);
            SDL_SetRenderDrawColor(renderer, 255, 205, 50, 255);
            switch_app::text(renderer, "A: SELECT   B: RESUME GAME", 40, 660, 2, 60, 1);
            SDL_RenderPresent(renderer);
            svcSleepThread(kFrameNs);
            next_frame_ns = 0;
            continue;
        }

        if (next_frame_ns == 0) { // first frame after the menu: start a fresh perf window
            window_start = armGetSystemTick();
            frames = busy = emu = render = 0;
        }
        const uint64_t t0 = armGetSystemTick();
        segas32::Inputs in = map_inputs(pad, roll, game_previous);
        for (int p = 0; p < switch_app::Pads::kPlayers; ++p) input_stats[p].add(in, pad[p], p);
        last_track_x = in.track_x[0], last_track_y = in.track_y[0];
        in.test = test_switch;
        try {
            game->run_frame(in); // hands the frame to the render worker at VBLANK start
            emu += armGetSystemTick() - t0;
            audio.push(game->audio());
        } catch (const std::exception &e) {
            status = std::string("Error: ") + e.what();
            worker.reset();
            game.reset();
            audio.pause();
            menu = true;
            continue;
        }
        uint64_t render_ticks = 0;
        if (worker->take(pixels, width, render_ticks)) {
            render += render_ticks;
            const SDL_Rect upd{0, 0, width, kScreenHeight};
            SDL_UpdateTexture(screen_texture, &upd, pixels.data(), width * int(sizeof(uint32_t)));
        }
        const SDL_Rect src{0, 0, width, kScreenHeight};
        busy += armGetSystemTick() - t0;
        ++frames;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const int dst_width = kDisplayHeight * 4 / 3; // 4:3 whatever the game's pixel width
        const SDL_Rect dst{(kDisplayWidth - dst_width) / 2, 0, dst_width, kDisplayHeight};
        SDL_RenderCopy(renderer, screen_texture, &src, &dst);
        if (show_perf) {
            char line[96];
            std::snprintf(line, sizeof line, "FPS: %.1f  EMU: %.2fMS  VIDEO: %.2fMS  AUDIO: %.0fMS  P1 X:%+d Y:%+d", fps, emu_ms,
                          render_ms, audio.queued_ms(), last_track_x, last_track_y);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_Rect bar{20, 10, 900, 28};
            SDL_RenderFillRect(renderer, &bar);
            SDL_SetRenderDrawColor(renderer, 50, 255, 100, 255);
            switch_app::text(renderer, line, 30, 16, 2, 70, 1);
        }
        SDL_RenderPresent(renderer);

        // Perf log every 2 s.
        const uint64_t now = armGetSystemTick();
        const double window_s = double(armTicksToNs(now - window_start)) / 1e9;
        if (window_s >= 2.0) {
            fps = double(frames) / window_s;
            busy_ms = frames ? double(armTicksToNs(busy)) / 1e6 / double(frames) : 0.0;
            emu_ms = frames ? double(armTicksToNs(emu)) / 1e6 / double(frames) : 0.0;
            render_ms = frames ? double(armTicksToNs(render)) / 1e6 / double(frames) : 0.0;
            if (perf_log) {
                std::fprintf(perf_log, "perf: fps=%.2f frame_ms=%.2f emu_ms=%.2f video_ms=%.2f audio_q_ms=%.1f\n", fps,
                             busy_ms, emu_ms, render_ms, audio.queued_ms());
                for (int p = 0; p < switch_app::Pads::kPlayers; ++p) {
                    const InputStats &st = input_stats[p];
                    if (st.buttons || st.x_frames || st.y_frames)
                        std::fprintf(perf_log, "input: p%d x_frames=%d x_max=%d y_frames=%d y_max=%d stick_x_max=%.2f buttons=%04x\n",
                                     p + 1, st.x_frames, st.x_max, st.y_frames, st.y_max, st.lx_max, st.buttons);
                }
                std::fflush(perf_log);
            }
            for (auto &st : input_stats) st = InputStats{};
            window_start = now;
            frames = busy = emu = render = 0;
        }

        // 60 Hz against a running deadline (resync after a stall).
        const uint64_t now_ns = armTicksToNs(armGetSystemTick());
        if (next_frame_ns == 0 || now_ns > next_frame_ns + 3 * kFrameNs) next_frame_ns = now_ns;
        next_frame_ns += kFrameNs;
        if (now_ns < next_frame_ns) {
            const uint64_t sleep_ns = next_frame_ns - now_ns;
            if (sleep_ns > 1'000'000ULL) svcSleepThread(sleep_ns - 800'000ULL);
            while (armTicksToNs(armGetSystemTick()) < next_frame_ns) {
            }
        }
    }

    worker.reset();
    audio.close();
    if (perf_log) {
        std::fprintf(perf_log, "=== %s SWITCH LOG END ===\n", kAppName);
        std::fclose(perf_log);
    }
    SDL_DestroyTexture(screen_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
