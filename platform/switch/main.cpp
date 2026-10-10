// Nintendo Switch app for the SegaSonic the Hedgehog (System 32) port.
// Step 1 shell: menu, input, ROM check, 4:3 presentation, 60 Hz pacing and
// the perf log. The board (V60, Z80, video, sound) plugs in at `Game`.
#define SDL_MAIN_HANDLED
#include "controls.h"
#include "text.h"
#include "core/rom_set.h"

#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
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

// Stand-in until the board exists: holds the ROM images and draws a test
// screen (colour bars and the controllers' state) so the shell can be
// checked on hardware.
class Game {
public:
    explicit Game(segas32::RomImages images) : img_(std::move(images)), screen_(size_t(kScreenWidth) * kScreenHeight) {}

    void run_frame(const switch_app::Pad (&pads)[switch_app::Pads::kPlayers]) {
        ++frame_;
        static constexpr uint32_t bars[] = {0xffc0c0c0, 0xffc0c000, 0xff00c0c0, 0xff00c000,
                                            0xffc000c0, 0xffc00000, 0xff0000c0, 0xff101010};
        for (int y = 0; y < kScreenHeight; ++y)
            for (int x = 0; x < kScreenWidth; ++x) {
                uint32_t c = bars[x * 8 / kScreenWidth];
                if (y >= 160) c = ((x + int(frame_)) / 8 + y / 8) & 1 ? 0xff202020 : 0xff606060;
                screen_[size_t(y) * kScreenWidth + x] = c;
            }
        // One marker per player, moved by the left stick (the trackball later).
        for (int p = 0; p < switch_app::Pads::kPlayers; ++p) {
            pos_[p][0] = std::clamp(pos_[p][0] + pads[p].lx * 4.0f, 0.0f, float(kScreenWidth - 9));
            pos_[p][1] = std::clamp(pos_[p][1] - pads[p].ly * 4.0f, 0.0f, float(kScreenHeight - 9));
            static constexpr uint32_t colours[] = {0xff2060ff, 0xffff4040, 0xffffe040};
            const uint32_t c = pads[p].buttons & (switch_app::A | switch_app::B) ? 0xffffffff : colours[p];
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx)
                    screen_[size_t(int(pos_[p][1]) + dy) * kScreenWidth + size_t(int(pos_[p][0]) + dx)] = c;
        }
    }

    const std::vector<uint32_t> &screen() const { return screen_; }

private:
    segas32::RomImages img_;
    std::vector<uint32_t> screen_;
    uint64_t frame_ = 0;
    float pos_[switch_app::Pads::kPlayers][2] = {{100, 100}, {200, 100}, {300, 100}};
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

    std::unique_ptr<Game> game;
    bool menu = true, running = true, wait_release = false, show_perf = false;
    int selection = 0;
    uint32_t previous = 0;
    std::string status = "Place sonic.zip (MAME set sonic) in sdmc:/switch/sonic/ then choose START GAME.";
    const std::string rom_path = find_rom_file();

    uint64_t next_frame_ns = 0, window_start = armGetSystemTick(), frames = 0, busy = 0;
    double fps = 0.0, busy_ms = 0.0;

    auto start_game = [&]() {
        try {
            game = std::make_unique<Game>(segas32::load_sonic(rom_path));
            status = "ROM OK (18 chips, rev. C). The board is not emulated yet: test screen.";
            return true;
        } catch (const std::exception &e) {
            status = std::string("Error: ") + e.what();
            game.reset();
            return false;
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
        }

        if (menu) {
            constexpr int kItems = 4;
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
                show_perf ? "PERFORMANCE OVERLAY: [ON]" : "PERFORMANCE OVERLAY: [OFF]", "EXIT TO HOMEBREW MENU"};
            for (int i = 0; i < kItems; ++i) {
                if (i == selection) SDL_SetRenderDrawColor(renderer, 255, 205, 50, 255);
                else SDL_SetRenderDrawColor(renderer, 210, 210, 220, 255);
                switch_app::text(renderer, (i == selection ? "> " : "  ") + labels[i], 60, 90 + i * 35, 2, 60, 1);
            }
            SDL_SetRenderDrawColor(renderer, 180, 180, 190, 255);
            switch_app::text(renderer, status, 40, 300, 2, 70, 3);
            SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
            switch_app::text(renderer, "PLAYERS 1-3: ONE CONTROLLER EACH (TRACKBALL ON THE LEFT STICK)", 40, 560, 2, 70, 1);
            switch_app::text(renderer, "PLUS=START   MINUS=COIN   PLUS+MINUS=MENU", 40, 590, 2, 70, 1);
            SDL_SetRenderDrawColor(renderer, 255, 205, 50, 255);
            switch_app::text(renderer, "A: SELECT   B: RESUME GAME", 40, 660, 2, 60, 1);
            SDL_RenderPresent(renderer);
            svcSleepThread(kFrameNs);
            next_frame_ns = 0;
            continue;
        }

        if (next_frame_ns == 0) { // first frame after the menu: start a fresh perf window
            window_start = armGetSystemTick();
            frames = busy = 0;
        }
        const uint64_t t0 = armGetSystemTick();
        game->run_frame(pad);
        SDL_UpdateTexture(screen_texture, nullptr, game->screen().data(), kScreenWidth * int(sizeof(uint32_t)));
        busy += armGetSystemTick() - t0;
        ++frames;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const int width = kDisplayHeight * 4 / 3; // 4:3 whatever the game's pixel width
        const SDL_Rect dst{(kDisplayWidth - width) / 2, 0, width, kDisplayHeight};
        SDL_RenderCopy(renderer, screen_texture, nullptr, &dst);
        if (show_perf) {
            char line[96];
            std::snprintf(line, sizeof line, "FPS: %.1f  FRAME: %.2fMS", fps, busy_ms);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_Rect bar{20, 10, 500, 28};
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
            if (perf_log) {
                std::fprintf(perf_log, "perf: fps=%.2f frame_ms=%.2f\n", fps, busy_ms);
                std::fflush(perf_log);
            }
            window_start = now;
            frames = busy = 0;
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
