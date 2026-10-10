// Sega System 32 main board as SegaSonic uses it (trackball I/O board),
// after MAME 0.289 src/mame/sega/segas32.cpp, segas32_v.cpp (register side),
// 315_5296.cpp, devices/machine/eepromser.cpp and upd4701.cpp. BSD-3-Clause
// (those files' licence); see THIRD_PARTY.md.
//
// Time: event times are kept in attoseconds as MAME's scheduler keeps them
// and converted to V60 clocks (16.10795 MHz) for the CPU, so interrupts land
// on the same instruction as in MAME. As in MAME's screen, time 0 is the
// start of VBLANK (line 224 of 262); the first VBLANK interrupt comes one
// frame later and VBLANK ends 38 lines after it starts. run_frame() runs
// up to and including the next VBLANK start, where MAME reports a frame.
#pragma once

#include "core/rom_set.h"
#include "cpu/v60/v60.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace segas32 {

// Cabinet inputs, active high here (the board inverts as the hardware does).
struct Inputs {
    bool coin[3] = {}, start[3] = {}, button[3] = {};
    bool service = false, test = false;
    int16_t track_x[3] = {}, track_y[3] = {}; // trackball movement this frame (counts)
};

// 93C46 serial EEPROM, 64 x 16 bits (MAME eeprom_serial_93cxx with
// eeprom_base_device's operation times: write 1.75 ms, erase 1 ms, write/erase
// all 8 ms; DO reads "busy" until then).
class Eeprom93c46 {
public:
    std::array<uint16_t, 64> data;
    std::function<uint64_t()> clock; // V60 clocks
    Eeprom93c46() { data.fill(0xffff); }
    void cs_write(int state);
    void clk_write(int state);
    void di_write(int state) { di_ = state & 1; }
    int do_read() const;

private:
    enum State { InReset, WaitStart, WaitCommand, Reading, WaitData, WaitDone };
    void handle(int event);
    void execute_command();
    bool ready() const { return !clock || clock() >= completion_; }
    void busy_for(double seconds);
    State state_ = InReset;
    uint64_t completion_ = 0, cs_rise_ = 0;
    int cs_ = 0, clk_ = 0, di_ = 0;
    bool locked_ = true;
    int bits_ = 0;
    uint32_t accum_ = 0, shift_ = 0;
    int command_ = 0, address_ = 0;
};

// uPD4701A trackball counter (12-bit X/Y counters, latched on read).
struct Upd4701 {
    uint16_t x = 0, y = 0, startx = 0, starty = 0;
    uint8_t read_xy(int offset) const {
        const uint16_t latch = offset & 2 ? uint16_t((y - starty) & 0xfff) : uint16_t((x - startx) & 0xfff);
        return uint8_t(offset & 1 ? latch >> 8 : latch);
    }
    void reset_xy() {
        startx = x;
        starty = y;
    }
};

class System32 : public Bus16 {
public:
    static constexpr double kV60Clock = 32215900.0 / 2;
    using atto = unsigned __int128;
    static constexpr uint64_t kAttoPerSecond = 1000000000000000000ull;
    static constexpr uint64_t kAttoPerCycle = kAttoPerSecond / 16107950; // HZ_TO_ATTOSECONDS
    static constexpr uint64_t kFramePeriod = kAttoPerSecond / 60;
    static constexpr uint64_t kScanTime = kFramePeriod / 262;
    static constexpr int kLines = 262, kVblankLine = 224;

    explicit System32(RomImages images);

    void reset();
    void run_frame(const Inputs &in);

    v60_device &cpu() { return *cpu_; }
    uint64_t frame() const { return frame_; }
    uint64_t now() const { return now_; }
    const std::vector<uint8_t> &work_ram() const { return work_; }
    std::vector<uint8_t> &work_ram() { return work_; }
    Eeprom93c46 &eeprom() { return eeprom_; }
    // Video state for the renderer (step 4).
    const std::vector<uint16_t> &video_ram() const { return videoram_; }
    const std::vector<uint16_t> &sprite_ram() const { return spriteram_; }
    const std::vector<uint16_t> &palette_ram() const { return palette_; }
    const std::array<uint16_t, 0x40> &mixer() const { return mixer_; }
    const std::array<uint8_t, 8> &sprite_control_latched() const { return sprite_control_latched_; }

    // Bus16
    uint16_t read16(uint32_t addr, uint16_t mask) override;
    void write16(uint32_t addr, uint16_t data, uint16_t mask) override;

    // Called with each 16-bit value the game reads from the random number
    // port (0xd80000), and may replace it (the oracle feeds MAME's values).
    std::function<uint16_t(uint16_t)> random_hook;

private:
    // events, in V60 clocks
    struct Event {
        atto when;
        int kind;
        int param;
    };
    enum { EvVblankStart, EvVblankEnd, EvSprites, EvTimer0, EvTimer1 };
    void schedule(atto when, int kind, int param = 0);
    void cancel(int kind);
    void run_until(atto when);
    uint64_t clock_now() const {
        return slice_len_ ? slice_start_ + uint64_t(slice_len_ - cpu_->cycles_left()) : now_;
    }
    atto atto_now() const { return atto(clock_now()) * kAttoPerCycle; }
    void dispatch(const Event &e);

    // interrupt controller
    void update_irq_state();
    void signal_v60_irq(int which);
    int irq_callback();
    uint8_t int_control_r(int offset);
    void int_control_w(int offset, uint8_t data);

    // I/O chip (315-5296)
    uint8_t io_read(int offset);
    void io_write(int offset, uint8_t data);
    uint8_t io_in_port(int port);
    void io_out_port(int port, uint8_t data);

    uint8_t sprite_control_r(int offset);
    void update_sprites();
    uint16_t palette_r(uint32_t offset);
    void palette_w(uint32_t offset, uint16_t data, uint16_t mask);

    RomImages img_;
    std::unique_ptr<v60_device> cpu_;
    std::vector<uint8_t> work_, shared_, comm_;
    std::vector<uint16_t> videoram_, spriteram_, palette_;
    std::array<uint16_t, 0x40> mixer_{};
    std::array<uint8_t, 8> sprite_control_{}, sprite_control_latched_{};
    int sprite_render_count_ = 0;
    bool sprite_buffer_b_ = false; // MAME: layer num of SPRITES vs SPRITES_2 swapped
    uint8_t irq_control_[16]{};
    uint8_t io_latch_[8]{}, io_dir_ = 0, io_cnt_ = 0;
    uint8_t tilebank_external_ = 0, display_enable_ = 0;
    bool sound_reset_ = true;
    uint8_t comm_cn_ = 0, comm_fg_ = 0, comm_zfg_ = 0;
    uint32_t rand_seed_ = 0x9d14abd7;
    Eeprom93c46 eeprom_;
    Upd4701 upd_[3];
    Inputs in_;

    std::vector<Event> events_;
    uint64_t now_ = 0, frame_ = 0, slice_start_ = 0, slice_target_ = 0;
    int slice_len_ = 0;
    bool vblank_due_ = false; // stopped at VBLANK start; deliver it first next frame
    bool aborted_ = false; // a timer was set inside the running slice (MAME: abort_timeslice)
    bool irq_line_ = false;
};

} // namespace segas32
