// Sega System 32 board as SegaSonic uses it (trackball I/O board), after
// MAME 0.289 src/mame/sega/segas32.cpp, segas32_v.cpp (register side),
// 315_5296.cpp, devices/machine/eepromser.cpp, eeprom.cpp, upd4701.cpp,
// devices/sound/ymfm_mame.h. BSD-3-Clause (those files' licence); see
// THIRD_PARTY.md.
//
// Main side: V60, memory map, interrupt controller and timers, I/O chip,
// EEPROM, trackball counters, video registers. Sound side: Z80, ROM banking,
// sound interrupt controller, two YM3438 (ymfm) and the RF5C68. Both CPUs
// and all timers run on a copy of MAME's scheduler (board/scheduler.h), so
// the CPUs interleave and interrupts land as in MAME. As in MAME's screen,
// time 0 is the start of VBLANK (line 224 of 262); run_frame() runs to the
// next VBLANK start, where MAME reports a frame.
#pragma once

#include "board/rf5c68.h"
#include "board/scheduler.h"
#include "board/video32.h"
#include "core/rom_set.h"
#include "cpu/v60/v60.h"
#include "cpu/z80/z80.h"
#include "ymfm_opn.h"

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
    std::function<Scheduler::atto()> clock;
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
    void busy_for(uint64_t usec);
    State state_ = InReset;
    Scheduler::atto completion_ = 0, cs_rise_ = 0;
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

// One YM3438 with MAME's ymfm glue (ym_generic_device / ymfm_device_base).
class Ym3438 : public ymfm::ymfm_interface {
public:
    static constexpr uint32_t kClock = 32215900 / 4;
    Ym3438(Scheduler &sched, std::function<void(bool)> irq);
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
    void reset() { chip_.reset(); }
    void update_stream(); // generate samples up to the current time
    std::vector<int16_t> &samples() { return out_; } // stereo, at sample_rate()
    uint32_t sample_rate() const { return chip_.sample_rate(kClock); }
    uint64_t generated() const { return generated_; }

protected:
    void ymfm_sync_mode_write(uint8_t data) override;
    void ymfm_sync_check_interrupts() override;
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
    void ymfm_update_irq(bool asserted) override { irq_(asserted); }
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;

private:
    Scheduler &sched_;
    std::function<void(bool)> irq_;
    ymfm::ym3438 chip_;
    Scheduler::Timer timer_[2];
    Scheduler::atto busy_end_ = 0;
    uint64_t generated_ = 0;
    std::vector<int16_t> out_;
};

class System32 : public Bus16 {
public:
    static constexpr double kV60Clock = 32215900.0 / 2;
    static constexpr int kLines = 262, kVblankLine = 224;
    using atto = Scheduler::atto;
    static constexpr uint64_t kAttoPerSecond = Scheduler::kAttoPerSecond;
    static constexpr uint64_t kFramePeriod = kAttoPerSecond / 60; // HZ_TO_ATTOSECONDS(60)
    static constexpr uint64_t kScanTime = kFramePeriod / 262;
    static constexpr int kAudioRate = 48000;

    explicit System32(RomImages images);
    ~System32() override;

    void reset();
    void run_frame(const Inputs &in);
    // Called at VBLANK start before the interrupt reaches the CPU (MAME's
    // frame_done point), e.g. to log or render.
    std::function<void()> on_frame;

    v60_device &cpu() { return *cpu_; }
    z80_device &sound_cpu() { return *z80_; }
    uint64_t frame() const { return frame_; }
    const std::vector<uint8_t> &work_ram() const { return work_; }
    std::vector<uint8_t> &work_ram() { return work_; }
    const std::vector<uint8_t> &shared_ram() const { return shared_; }
    Eeprom93c46 &eeprom() { return eeprom_; }

    int render() { return video_->render(video_inputs()); }
    const std::vector<uint32_t> &frame_pixels() const { return video_->frame(); }

    // Audio of the last frame, stereo int16 at kAudioRate (MAME's routes: each
    // YM3438 at 0.30, the RF5C68 at 0.40).
    const std::vector<int16_t> &audio() const { return audio_; }

    // Bus16 (V60)
    uint16_t read16(uint32_t addr, uint16_t mask) override;
    void write16(uint32_t addr, uint16_t data, uint16_t mask) override;

    std::function<uint16_t(uint16_t)> random_hook;

private:
    struct SoundBus : Bus8 {
        System32 &b;
        explicit SoundBus(System32 &s) : b(s) {}
        uint8_t read8(uint16_t a) override { return b.z80_read(a); }
        void write8(uint16_t a, uint8_t d) override { b.z80_write(a, d); }
    };
    struct SoundIo : Bus8 {
        System32 &b;
        explicit SoundIo(System32 &s) : b(s) {}
        uint8_t read8(uint16_t a) override { return b.z80_in(uint8_t(a)); }
        void write8(uint16_t a, uint8_t d) override { b.z80_out(uint8_t(a), d); }
    };

    Video32::Inputs video_inputs() const {
        return {videoram_.data(), spriteram_.data(), palette_.data(), mixer_.data(), sprite_control_latched_.data(),
                tilebank_external_, display_enable_ != 0};
    }
    atto now() const { return sched_.time(); }

    // timers
    void vblank_begin();
    void vblank_end_int();

    // input lines (device_input::set_state_synced)
    void v60_irq_line(int state);
    void z80_irq_line(int state, int vector);
    void z80_reset_line(int state);

    // main interrupt controller
    void update_irq_state();
    void signal_v60_irq(int which);
    int irq_callback();
    uint8_t int_control_r(int offset);
    void int_control_w(int offset, uint8_t data);

    // sound interrupt controller, banking, Z80 buses
    void update_sound_irq_state();
    void signal_sound_irq(int which);
    void clear_sound_irq(int which);
    uint8_t z80_read(uint16_t a);
    void z80_write(uint16_t a, uint8_t d);
    uint8_t z80_in(uint8_t port);
    void z80_out(uint8_t port, uint8_t d);
    void update_rf_stream();
    void mix_audio();

    // I/O chip (315-5296)
    uint8_t io_read(int offset);
    void io_write(int offset, uint8_t data);
    uint8_t io_in_port(int port);
    void io_out_port(int port, uint8_t data);
    void io_out_cnt(uint8_t data);

    uint8_t sprite_control_r(int offset);
    void update_sprites();
    uint16_t palette_r(uint32_t offset);
    void palette_w(uint32_t offset, uint16_t data, uint16_t mask);

    RomImages img_;
    Scheduler sched_;
    Scheduler::Exec v60_exec_, z80_exec_;
    std::unique_ptr<v60_device> cpu_;
    SoundBus sound_bus_{*this};
    SoundIo sound_io_{*this};
    std::unique_ptr<z80_device> z80_;
    std::unique_ptr<Video32> video_;
    std::unique_ptr<Ym3438> ym_[2];
    Rf5c68 rf_;
    uint64_t rf_generated_ = 0;
    std::vector<int16_t> rf_out_, audio_;
    uint64_t audio_out_ = 0; // 48 kHz samples produced so far

    Scheduler::Timer t_vblank_begin_, t_vblank_end_int_, t_sprites_, t_irq0_, t_irq1_, t_sound_update_;

    std::vector<uint8_t> work_, shared_, comm_;
    std::vector<uint16_t> videoram_, spriteram_, palette_;
    std::array<uint16_t, 0x40> mixer_{};
    std::array<uint8_t, 8> sprite_control_{}, sprite_control_latched_{};
    int sprite_render_count_ = 0;
    uint8_t irq_control_[16]{};
    uint8_t sound_irq_control_[4]{}, sound_irq_input_ = 0, sound_dummy_ = 0;
    uint16_t sound_bank_ = 0;
    uint8_t io_latch_[8]{}, io_dir_ = 0, io_cnt_ = 0;
    uint8_t tilebank_external_ = 0, display_enable_ = 0;
    uint8_t comm_cn_ = 0, comm_fg_ = 0, comm_zfg_ = 0;
    uint32_t rand_seed_ = 0x9d14abd7;
    Eeprom93c46 eeprom_;
    Upd4701 upd_[3];
    Inputs in_;
    // queued input-line events per line (device_input), applied by one sync timer
    std::vector<int> v60_irq_queue_, z80_irq_queue_, z80_reset_queue_;
    int z80_vector_ = 0xff;

    uint64_t frame_ = 0;
    bool frame_done_ = false;
};

} // namespace segas32
