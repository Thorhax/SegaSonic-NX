#include "board/system32.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace segas32 {

namespace {

constexpr int MAIN_IRQ_VBSTART = 0;
constexpr int MAIN_IRQ_VBSTOP = 1;
constexpr int MAIN_IRQ_SOUND = 2;
constexpr int MAIN_IRQ_TIMER0 = 3;
constexpr int MAIN_IRQ_TIMER1 = 4;

// Calls fn(byte_offset, lane) for each byte lane in `mask` of a word access
// at `addr` (8-bit handlers without umask on MAME's 16-bit bus).
template <typename F>
void each_lane(uint32_t addr, uint16_t mask, F fn) {
    if (mask & 0x00ff) fn(addr & ~1u, 0);
    if (mask & 0xff00) fn(addr | 1u, 1);
}

uint16_t xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(uint16_t value) {
    const int r = (value >> 0) & 0x1f;
    const int g = (value >> 5) & 0x1f;
    const int b = (value >> 10) & 0x1f;
    value = uint16_t((value & 0x8000) | ((b & 0x01) << 14) | ((g & 0x01) << 13) | ((r & 0x01) << 12));
    value = uint16_t(value | ((b & 0x1e) << 7) | ((g & 0x1e) << 3) | ((r & 0x1e) >> 1));
    return value;
}

uint16_t xBGRBBBBGGGGRRRR_to_xBBBBBGGGGGRRRRR(uint16_t value) {
    const int r = ((value >> 12) & 0x01) | ((value << 1) & 0x1e);
    const int g = ((value >> 13) & 0x01) | ((value >> 3) & 0x1e);
    const int b = ((value >> 14) & 0x01) | ((value >> 7) & 0x1e);
    return uint16_t((value & 0x8000) | (b << 10) | (g << 5) | (r << 0));
}

void combine(uint16_t &dst, uint16_t data, uint16_t mask) { dst = uint16_t((dst & ~mask) | (data & mask)); }

} // namespace

// ---------------------------------------------------------------------------
// 93C46 (eepromser.cpp, 93cxx command set, 6 address bits, 16 data bits)

enum { EvCsRise = 1, EvCsFall, EvClkRise, EvClkFall };

void Eeprom93c46::busy_for(uint64_t usec) { // attotime::from_usec
    if (clock) completion_ = clock() + Scheduler::atto(usec) * 1000000000000ull;
}

void Eeprom93c46::cs_write(int state) {
    state &= 1;
    if (state == cs_) return;
    cs_ = state;
    if (cs_ && clock) cs_rise_ = clock();
    handle(cs_ ? EvCsRise : EvCsFall);
}

void Eeprom93c46::clk_write(int state) {
    state &= 1;
    if (state == clk_) return;
    clk_ = state;
    handle(clk_ ? EvClkRise : EvClkFall);
}

int Eeprom93c46::do_read() const {
    if (state_ == WaitStart) return ready() ? 1 : 0;
    if (state_ == Reading) return (shift_ & 0x80000000u) ? 1 : 0;
    return 1; // tri-state, pulled up
}

void Eeprom93c46::handle(int event) {
    switch (state_) {
    case InReset:
        if (event == EvCsRise) state_ = WaitStart;
        break;
    case WaitStart:
        if (event == EvClkRise && di_ && ready() && (!clock || clock() > cs_rise_)) {
            accum_ = 0;
            bits_ = 0;
            state_ = WaitCommand;
        } else if (event == EvCsFall) state_ = InReset;
        break;
    case WaitCommand:
        if (event == EvClkRise) {
            accum_ = (accum_ << 1) | uint32_t(di_);
            if (++bits_ == 2 + 6) execute_command();
        } else if (event == EvCsFall) state_ = InReset;
        break;
    case Reading:
        if (event == EvClkRise) {
            const int bit_index = bits_++;
            if (bit_index % 16 == 0 && bit_index == 0) // no streaming
                shift_ = uint32_t(data[(address_ + bits_ / 16) & 63]) << 16;
            else
                shift_ = (shift_ << 1) | 1;
        } else if (event == EvCsFall) state_ = InReset;
        break;
    case WaitData:
        if (event == EvClkRise) {
            shift_ = (shift_ << 1) | uint32_t(di_);
            if (++bits_ == 16) {
                if (locked_) state_ = InReset;
                else {
                    if (command_ == 1) {
                        data[address_ & 63] = uint16_t(shift_);
                        busy_for(1750);
                    } else {
                        data.fill(uint16_t(shift_)); // write all
                        busy_for(8000);
                    }
                    state_ = WaitDone;
                }
            }
        } else if (event == EvCsFall) state_ = InReset;
        break;
    case WaitDone:
        if (event == EvCsFall) state_ = InReset;
        break;
    }
}

// command_: 1 write, 2 read, 3 erase, 4 lock, 5 write all, 6 erase all, 7 unlock
void Eeprom93c46::execute_command() {
    address_ = int(accum_ & 63);
    switch (accum_ >> 6) {
    case 0:
        command_ = 4 + int(address_ >> 4); // lock, write all, erase all, unlock
        address_ = 0;
        break;
    case 1: command_ = 1; break;
    case 2: command_ = 2; break;
    case 3: command_ = 3; break;
    }
    bits_ = 0;
    switch (command_) {
    case 2:
        shift_ = 0;
        state_ = Reading;
        break;
    case 1:
    case 5:
        shift_ = 0;
        state_ = WaitData;
        break;
    case 3:
        if (locked_) state_ = InReset;
        else {
            data[address_] = 0xffff;
            busy_for(1000);
            state_ = WaitDone;
        }
        break;
    case 4:
        locked_ = true;
        state_ = InReset;
        break;
    case 7:
        locked_ = false;
        state_ = InReset;
        break;
    case 6:
        if (locked_) state_ = InReset;
        else {
            data.fill(0xffff);
            busy_for(8000);
            state_ = WaitDone;
        }
        break;
    }
}


// --- YM3438 (ymfm_mame.h glue) -------------------------------------------------

Ym3438::Ym3438(Scheduler &sched, std::function<void(bool)> irq) : sched_(sched), irq_(std::move(irq)), chip_(*this) {
    for (int t = 0; t < 2; ++t) timer_[t].callback = [this](int p) { m_engine->engine_timer_expired(uint32_t(p)); };
}

void Ym3438::update_stream() {
    const uint64_t target = uint64_t(sched_.time() * sample_rate() / Scheduler::kAttoPerSecond);
    while (generated_ < target) {
        ymfm::ym3438::output_data o;
        chip_.generate(&o, 1);
        out_.push_back(int16_t(std::clamp(o.data[0], -32768, 32767)));
        out_.push_back(int16_t(std::clamp(o.data[1], -32768, 32767)));
        ++generated_;
    }
}

uint8_t Ym3438::read(int offset) {
    update_stream();
    return chip_.read(uint32_t(offset));
}

void Ym3438::write(int offset, uint8_t data) {
    update_stream();
    chip_.write(uint32_t(offset), data);
}

void Ym3438::ymfm_sync_mode_write(uint8_t data) {
    sched_.synchronize([this](int p) { m_engine->engine_mode_write(uint8_t(p)); }, data);
}

void Ym3438::ymfm_sync_check_interrupts() {
    if (sched_.executing()) sched_.synchronize([this](int) { m_engine->engine_check_interrupts(); });
    else m_engine->engine_check_interrupts();
}

void Ym3438::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) {
    if (duration_in_clocks >= 0) sched_.adjust(timer_[tnum], Scheduler::from_ticks(uint64_t(duration_in_clocks), kClock), int(tnum));
    else sched_.enable(timer_[tnum], false);
}

void Ym3438::ymfm_set_busy_end(uint32_t clocks) { busy_end_ = sched_.time() + Scheduler::from_ticks(clocks, kClock); }

bool Ym3438::ymfm_is_busy() { return sched_.time() < busy_end_; }

// ---------------------------------------------------------------------------

namespace {
constexpr int SOUND_IRQ_YM3438 = 0;
constexpr int SOUND_IRQ_V60 = 1;
} // namespace

System32::System32(RomImages images)
    : img_(std::move(images)), work_(0x10000), shared_(0x2000), comm_(0x800), videoram_(0x10000),
      spriteram_(0x10000), palette_(0x4000) {
    cpu_ = std::make_unique<v60_device>(*this);
    z80_ = std::make_unique<z80_device>(sound_bus_, sound_io_);
    video_ = std::make_unique<Video32>(img_.tiles, img_.sprites);
    ym_[0] = std::make_unique<Ym3438>(sched_, [this](bool on) { // ym3438_irq_handler
        if (on) signal_sound_irq(SOUND_IRQ_YM3438);
        else clear_sound_irq(SOUND_IRQ_YM3438);
    });
    ym_[1] = std::make_unique<Ym3438>(sched_, [](bool) {}); // IRQ not connected
    cpu_->irq_ack = [this] { return irq_callback(); };
    eeprom_.clock = [this] { return now(); };

    v60_exec_.clock = 16107950;
    v60_exec_.apc = Scheduler::hz_to_atto(v60_exec_.clock);
    v60_exec_.run = [this](int n) { cpu_->run(n); };
    v60_exec_.icount = [this] { return cpu_->cycles_left(); };
    v60_exec_.zero_icount = [this] { cpu_->abort_run(); };
    z80_exec_.clock = 32215900 / 4;
    z80_exec_.apc = Scheduler::hz_to_atto(z80_exec_.clock);
    z80_exec_.run = [this](int n) { z80_->run(n); };
    z80_exec_.icount = [this] { return z80_->cycles_left(); };
    z80_exec_.zero_icount = [this] { z80_->abort_run(); };
    sched_.add_exec(&v60_exec_);
    sched_.add_exec(&z80_exec_);

    t_vblank_begin_.callback = [this](int) { vblank_begin(); };
    t_vblank_end_int_.callback = [this](int) { vblank_end_int(); };
    t_sprites_.callback = [this](int) { update_sprites(); };
    t_irq0_.callback = [this](int p) { signal_v60_irq(p); };
    t_irq1_.callback = [this](int p) { signal_v60_irq(p); };
    t_sound_update_.callback = [this](int) { // sound_manager::update: bring the streams up to date
        ym_[0]->update_stream();
        ym_[1]->update_stream();
        update_rf_stream();
    };
    reset();
}

System32::~System32() = default;

void System32::reset() {
    std::fill(work_.begin(), work_.end(), 0);
    std::fill(shared_.begin(), shared_.end(), 0);
    std::fill(comm_.begin(), comm_.end(), 0);
    std::fill(videoram_.begin(), videoram_.end(), 0);
    std::fill(spriteram_.begin(), spriteram_.end(), 0);
    std::fill(palette_.begin(), palette_.end(), 0);
    videoram_[0x1ff00 / 2] = 0x8000; // segas32_v.cpp device_start
    video_->invalidate_all();
    mixer_.fill(0xffff);
    sprite_control_.fill(0);
    sprite_control_latched_.fill(0);
    sprite_render_count_ = 0;
    std::fill(std::begin(irq_control_), std::end(irq_control_), 0xff); // device_reset
    std::fill(std::begin(sound_irq_control_), std::end(sound_irq_control_), 0);
    sound_irq_input_ = 0;
    sound_dummy_ = 0;
    sound_bank_ = 0;
    rand_seed_ = 0x9d14abd7;
    // MAME's first frame update reads the reversed X trackball ports as 0xfff
    // against a last value of 0, so each X counter starts at 0xfff (-1); the
    // game reads that once at boot and then resets the counters.
    for (auto &u : upd_) u = Upd4701{0x0fff, 0, 0, 0};
    frame_ = 0;
    sched_.reset_time();
    cpu_->reset();
    z80_->reset();
    ym_[0]->reset();
    ym_[1]->reset();
    v60_irq_line(CLEAR_LINE); // segas32_state::device_reset
    // 315-5296 device_reset: all ports inputs, outputs and CNT pins low (so
    // the Z80 is held in reset until the game raises CNT2).
    std::fill(std::begin(io_latch_), std::end(io_latch_), 0);
    io_dir_ = io_cnt_ = 0;
    for (int i = 0; i < 8; ++i) io_out_port(i, 0);
    io_out_cnt(0);
    // timers: the screen's first VBLANK start one frame in (time 0 is VBLANK
    // start); the sound manager's periodic update every 1/50 s.
    sched_.adjust(t_vblank_begin_, kFramePeriod);
    sched_.adjust(t_sound_update_, kAttoPerSecond / 50, 0, kAttoPerSecond / 50);
}

void System32::run_frame(const Inputs &in) {
    in_ = in;
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
    mix_audio();
    ++frame_;
}

// screen_device::vblank_begin: frame update (render, frame notifiers), then
// the VBLANK callbacks (start_of_vblank_int), then the next VBLANK.
void System32::vblank_begin() {
    for (int p = 0; p < 3; ++p) { // upd4701 update() at MACHINE_NOTIFY_FRAME
        upd_[p].x = uint16_t((upd_[p].x + in_.track_x[p]) & 0xffff);
        upd_[p].y = uint16_t((upd_[p].y + in_.track_y[p]) & 0xffff);
    }
    if (on_frame) on_frame();
    frame_done_ = true;
    signal_v60_irq(0 /* MAIN_IRQ_VBSTART */);
    sched_.adjust(t_vblank_end_int_, atto(kLines - kVblankLine) * kScanTime); // time_until_pos(0)
    sched_.adjust(t_vblank_begin_, kFramePeriod);
}

void System32::vblank_end_int() { // end_of_vblank_int, then system32_set_vblank(0)
    signal_v60_irq(1 /* MAIN_IRQ_VBSTOP */);
    sched_.adjust(t_sprites_, atto(50) * 1000000000000ull, 1); // attotime::from_usec(50)
}

// --- input lines -------------------------------------------------------------------

void System32::v60_irq_line(int state) {
    v60_irq_queue_.push_back(state);
    if (v60_irq_queue_.size() == 1)
        sched_.synchronize([this](int) {
            for (int s : v60_irq_queue_) cpu_->set_input(0, s);
            v60_irq_queue_.clear();
        });
}

void System32::z80_irq_line(int state, int vector) {
    z80_irq_queue_.push_back(state | (vector << 8));
    if (z80_irq_queue_.size() == 1)
        sched_.synchronize([this](int) {
            for (int e : z80_irq_queue_) {
                z80_vector_ = e >> 8;
                z80_->irq_vector = z80_vector_;
                z80_->set_input(INPUT_LINE_IRQ0, e & 0xff);
            }
            z80_irq_queue_.clear();
        });
}

void System32::z80_reset_line(int state) {
    z80_reset_queue_.push_back(state);
    if (z80_reset_queue_.size() == 1)
        sched_.synchronize([this](int) {
            for (int s : z80_reset_queue_) {
                if (s == ASSERT_LINE) sched_.suspend(z80_exec_, true);
                else if (z80_exec_.next_suspended) {
                    z80_->reset(); // device().reset() then resume
                    sched_.suspend(z80_exec_, false);
                }
            }
            z80_reset_queue_.clear();
        });
}

// --- main interrupt controller ----------------------------------------------------------

void System32::update_irq_state() {
    const uint8_t effirq = irq_control_[7] & ~irq_control_[6] & 0x1f;
    v60_irq_line(effirq ? ASSERT_LINE : CLEAR_LINE);
}

void System32::signal_v60_irq(int which) {
    if (which >= 0 && which < 5) irq_control_[7] |= uint8_t(1 << which);
    update_irq_state();
}

int System32::irq_callback() {
    const uint8_t effirq = irq_control_[7] & ~irq_control_[6] & 0x1f;
    for (int vector = 0; vector < 5; vector++)
        if (BIT(effirq, vector)) return irq_control_[vector];
    return 0;
}

uint8_t System32::int_control_r(int) { return 0xff; } // timer counts: not modelled (as MAME)

void System32::int_control_w(int offset, uint8_t data) {
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5:
        irq_control_[offset] = data;
        break;
    case 6:
        irq_control_[offset] = data;
        update_irq_state();
        break;
    case 7:
        irq_control_[offset] &= data;
        update_irq_state();
        break;
    case 8: case 9: {
        irq_control_[offset] = data;
        const uint16_t duration = uint16_t(irq_control_[8] | ((irq_control_[9] << 8) & 0xf00));
        if (duration) sched_.adjust(t_irq0_, Scheduler::from_ticks(0x800u * duration, 16107950), 3 /* TIMER0 */);
        break;
    }
    case 10: case 11: {
        irq_control_[offset] = data;
        const uint16_t duration = uint16_t(irq_control_[10] | ((irq_control_[11] << 8) & 0xf00));
        if (duration) sched_.adjust(t_irq1_, Scheduler::from_ticks(0x100u * duration, 50000000 / 16), 4 /* TIMER1 */);
        break;
    }
    case 12: case 13: case 14: case 15:
        signal_sound_irq(SOUND_IRQ_V60);
        break;
    }
}

// --- sound side ----------------------------------------------------------------------------

void System32::update_sound_irq_state() {
    const uint8_t effirq = sound_irq_input_ & ~sound_irq_control_[3] & 0x07;
    int vector;
    for (vector = 0; vector < 3; vector++)
        if (BIT(effirq, vector)) {
            z80_irq_line(ASSERT_LINE, 2 * vector);
            break;
        }
    if (vector == 3) z80_irq_line(CLEAR_LINE, z80_vector_); // set_input_line keeps the stored vector
}

void System32::signal_sound_irq(int which) {
    for (int i = 0; i < 3; i++)
        if (sound_irq_control_[i] == which) sound_irq_input_ |= uint8_t(1 << i);
    update_sound_irq_state();
}

void System32::clear_sound_irq(int which) {
    for (int i = 0; i < 3; i++)
        if (sound_irq_control_[i] == which) sound_irq_input_ &= uint8_t(~(1 << i));
    update_sound_irq_state();
}

uint8_t System32::z80_read(uint16_t a) {
    if (a < 0xa000) return img_.sound_cpu[a];
    if (a < 0xc000) return img_.sound_cpu[(size_t(sound_bank_) * 0x2000 + (a & 0x1fff)) % img_.sound_cpu.size()];
    if (a < 0xe000) return rf_.read(uint16_t(a & 0x1fff));
    return shared_[a & 0x1fff];
}

void System32::z80_write(uint16_t a, uint8_t d) {
    if (a >= 0xe000) shared_[a & 0x1fff] = d;
    else if (a >= 0xc000) {
        update_rf_stream(); // m_stream->update() before register and wave writes
        rf_.write(uint16_t(a & 0x1fff), d);
    }
}

uint8_t System32::z80_in(uint8_t port) {
    if ((port & 0xf0) == 0x80) return ym_[0]->read(port & 3);
    if ((port & 0xf0) == 0x90) return ym_[1]->read(port & 3);
    if (port == 0xf1) return sound_dummy_;
    return 0xff; // unmap_value_high
}

void System32::z80_out(uint8_t port, uint8_t d) {
    switch (port & 0xf0) {
    case 0x80: ym_[0]->write(port & 3, d); break;
    case 0x90: ym_[1]->write(port & 3, d); break;
    case 0xa0: sound_bank_ = uint16_t((sound_bank_ & ~0x3f) | (d & 0x3f)); break;        // sound_bank_lo_w
    case 0xb0: sound_bank_ = uint16_t((sound_bank_ & 0x3f) | ((d & 0x04) << 4) | ((d & 0x03) << 7)); break; // hi
    case 0xc0: // sound_int_control_lo_w
        if (port & 1) {
            sound_irq_input_ &= d;
            update_sound_irq_state();
        }
        if (port & 4) signal_v60_irq(2 /* MAIN_IRQ_SOUND */);
        break;
    case 0xd0: // sound_int_control_hi_w (0xd0-0xd3, mirror 0x04)
        if ((port & 0x08) == 0) {
            sound_irq_control_[port & 3] = d;
            update_sound_irq_state();
        }
        break;
    case 0xf0:
        if (port == 0xf1) sound_dummy_ = d;
        break;
    }
}

void System32::update_rf_stream() {
    const uint64_t rate = 50000000 / 4 / 384;
    const uint64_t target = uint64_t(now() * rate / kAttoPerSecond);
    if (target > rf_generated_) {
        rf_.generate(int(target - rf_generated_), rf_out_);
        rf_generated_ = target;
    }
}

// Resample the three streams to 48 kHz (linear) and mix with MAME's route
// gains. Each buffer holds a stream's samples from absolute index
// (generated - size); output sample i is at time i / 48000.
void System32::mix_audio() {
    ym_[0]->update_stream();
    ym_[1]->update_stream();
    update_rf_stream();
    struct Src {
        std::vector<int16_t> *buf;
        uint64_t generated;
        double rate, gain;
    } src[3] = {{&ym_[0]->samples(), ym_[0]->generated(), double(ym_[0]->sample_rate()), 0.30},
                {&ym_[1]->samples(), ym_[1]->generated(), double(ym_[1]->sample_rate()), 0.30},
                {&rf_out_, rf_generated_, double(50000000 / 4 / 384), 0.40}};
    const uint64_t end = uint64_t(now() * kAudioRate / kAttoPerSecond); // output samples due by now
    const int n = int(end - audio_out_);
    audio_.assign(size_t(std::max(n, 0)) * 2, 0);
    std::vector<double> acc(audio_.size(), 0.0);
    for (const Src &s : src) {
        const size_t frames = s.buf->size() / 2;
        if (!frames) continue;
        const double base = double(s.generated - frames);
        for (int i = 0; i < n; ++i) {
            const double pos = double(audio_out_ + uint64_t(i)) * s.rate / kAudioRate - base;
            const size_t k = pos <= 0 ? 0 : size_t(pos);
            const double f = pos <= 0 ? 0 : pos - double(k);
            const size_t k0 = std::min(k, frames - 1), k1 = std::min(k + 1, frames - 1);
            for (int c = 0; c < 2; ++c) {
                const double a = (*s.buf)[k0 * 2 + size_t(c)], b = (*s.buf)[k1 * 2 + size_t(c)];
                acc[size_t(i) * 2 + size_t(c)] += (a + (b - a) * f) * s.gain;
            }
        }
        // drop what the next frame no longer needs (keep one sample back)
        const double next = double(end) * s.rate / kAudioRate - base;
        const size_t drop = next > 1 ? std::min(frames, size_t(next) - 1) : 0;
        s.buf->erase(s.buf->begin(), s.buf->begin() + ptrdiff_t(drop * 2));
    }
    for (size_t i = 0; i < acc.size(); ++i) audio_[i] = int16_t(std::clamp(acc[i], -32768.0, 32767.0));
    audio_out_ = end;
}

// --- I/O chip ----------------------------------------------------------------

uint8_t System32::io_in_port(int port) {
    switch (port) {
    case 0: { // P1_A: P1 button (0x01), P3 button (0x04)
        uint8_t v = 0xff;
        if (in_.button[0]) v &= ~0x01;
        if (in_.button[2]) v &= ~0x04;
        return v;
    }
    case 1: return in_.button[1] ? 0xfe : 0xff; // P2_A
    case 2: return 0xff;                        // PORTC_A
    case 4: { // SERVICE12_A
        uint8_t v = 0xff;
        if (in_.service) v &= ~0x01;
        if (in_.test) v &= ~0x02;
        if (in_.coin[0]) v &= ~0x04;
        if (in_.coin[1]) v &= ~0x08;
        if (in_.start[0]) v &= ~0x10;
        if (in_.start[1]) v &= ~0x20;
        if (in_.coin[2]) v &= ~0x40;
        if (in_.start[2]) v &= ~0x80;
        return v;
    }
    case 5: // SERVICE34_A: DIP switches off, PCB buttons up, EEPROM DO at bit 7
        return uint8_t(0x7f | (eeprom_.do_read() ? 0x80 : 0x00));
    default: return 0xff; // output ports read as inputs: nothing connected
    }
}

void System32::io_out_port(int port, uint8_t data) {
    switch (port) {
    case 3: // PD: misc_output_0 (coin counters), then EEPROM DI, CS, CLK
        eeprom_.di_write(BIT(data, 7));
        eeprom_.cs_write(BIT(data, 5));
        eeprom_.clk_write(BIT(data, 6));
        break;
    case 7: tilebank_external_ = data; break; // PH
    default: break;
    }
}

uint8_t System32::io_read(int offset) {
    offset &= 0x3f;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        if (BIT(io_dir_, offset)) return io_latch_[offset];
        return io_in_port(offset);
    case 0x8: return 'S';
    case 0x9: return 'E';
    case 0xa: return 'G';
    case 0xb: return 'A';
    case 0xc: case 0xe: return io_cnt_;
    case 0xd: case 0xf: return io_dir_;
    default: return 0xff;
    }
}

void System32::io_write(int offset, uint8_t data) {
    offset &= 0x3f;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        if (BIT(io_dir_, offset)) io_out_port(offset, data);
        io_latch_[offset] = data;
        break;
    case 0xe:
        io_out_cnt(data); // CNT0-2 output pins
        io_cnt_ = data;
        break;
    case 0xf:
        for (int i = 0; i < 8; i++)
            if (BIT(io_dir_ ^ data, i)) io_out_port(i, BIT(data, i) ? io_latch_[i] : 0);
        io_dir_ = data;
        break;
    default: break;
    }
}

// CNT1: display enable; CNT2: the Z80's RESET, inverted (every write sets the
// line, as MAME's devcb does).
void System32::io_out_cnt(uint8_t data) {
    display_enable_ = BIT(data, 1);
    z80_reset_line(BIT(data, 2) ? CLEAR_LINE : ASSERT_LINE);
}

// --- video-side registers ------------------------------------------------------

uint8_t System32::sprite_control_r(int offset) {
    switch (offset) {
    case 0: return uint8_t(0xfc | (video_->sprite_buffer_select() ? 1 : 0));
    case 1: return 0xfc | 1;
    case 2: return uint8_t(0xfc | sprite_control_latched_[2]);
    case 3: return uint8_t(0xfc | sprite_control_latched_[3]);
    case 4: return uint8_t(0xfc | sprite_control_latched_[4]);
    case 5: return uint8_t(0xfc | sprite_control_latched_[5]);
    case 6: return uint8_t(0xfc | (sprite_control_latched_[6] & 1));
    case 7: return 0xfc;
    }
    return 0xff;
}

void System32::update_sprites() {
    if (!BIT(sprite_control_[3], 1)) {
        if (sprite_render_count_-- == 0) {
            sprite_control_[0] = 3;
            sprite_render_count_ = sprite_control_[3] & 1;
        }
    }
    if (BIT(sprite_control_[0], 1)) video_->erase_sprites();
    if (BIT(sprite_control_[0], 0)) {
        video_->swap_sprite_buffers(); // sprite_swap_buffers: also latches the control registers
        sprite_control_latched_ = sprite_control_;
        video_->render_sprite_list(video_inputs());
    }
    sprite_control_[0] = 0;
}

uint16_t System32::palette_r(uint32_t offset) {
    const bool convert = BIT(offset, 14);
    offset &= 0x3fff;
    return convert ? xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(palette_[offset]) : palette_[offset];
}

void System32::palette_w(uint32_t offset, uint16_t data, uint16_t mask) {
    const bool convert = BIT(offset, 14);
    offset &= 0x3fff;
    for (int pass = 0; pass < 2; ++pass) {
        uint16_t value = palette_[offset];
        if (convert) value = xBBBBBGGGGGRRRRR_to_xBGRBBBBGGGGRRRR(value);
        combine(value, data, mask);
        if (convert) value = xBGRBBBBGGGGRRRR_to_xBBBBBGGGGGRRRRR(value);
        palette_[offset] = value;
        if (!(mixer_[0x4e / 2] & 0x0880)) break; // blending: writes go to both halves
        offset ^= 0x2000;
    }
}

// --- bus -------------------------------------------------------------------------

uint16_t System32::read16(uint32_t a, uint16_t mask) {
    a &= 0xfffffe;
    switch (a >> 20) {
    case 0x0: case 0x1:
        return uint16_t(img_.main_cpu[a] | img_.main_cpu[a + 1] << 8);
    case 0xf:
        return uint16_t(img_.main_cpu[a & 0xfffff] | img_.main_cpu[(a & 0xfffff) + 1] << 8);
    case 0x2: {
        const uint32_t o = a & 0xffff;
        return uint16_t(work_[o] | work_[o + 1] << 8);
    }
    case 0x3: return videoram_[(a & 0x1ffff) >> 1];
    case 0x4: return spriteram_[(a & 0x1ffff) >> 1];
    case 0x5: return uint16_t(0xff00 | sprite_control_r(int((a >> 1) & 7)));
    case 0x6:
        if (a & 0x10000) return mixer_[(a & 0x7f) >> 1];
        return palette_r((a & 0xffff) >> 1);
    case 0x7: {
        const uint32_t o = a & 0x1fff;
        return uint16_t(shared_[o] | shared_[o + 1] << 8);
    }
    case 0x8:
        if (a < 0x801000) return uint16_t(0xff00 | comm_[(a & 0xfff) >> 1]);
        if (a == 0x801000) return uint16_t(0xff00 | (comm_cn_ | 0xfe));
        if (a == 0x801002) return uint16_t(0xff00 | ((comm_fg_ | (~comm_zfg_ << 7) | 0x7e) & 0xff));
        return 0xffff;
    case 0xc: {
        const uint32_t o = a & 0x7f;
        if (o < 0x20) return uint16_t(0xff00 | io_read(int(o >> 1)));
        if (o >= 0x40 && o < 0x58) return uint16_t(0xff00 | upd_[(o - 0x40) >> 3].read_xy(int((o >> 1) & 3)));
        return 0xffff;
    }
    case 0xd:
        if (a < 0xd80000) {
            uint16_t v = 0;
            each_lane(a, mask, [&](uint32_t b, int lane) { v |= uint16_t(int_control_r(int(b & 15)) << (8 * lane)); });
            return uint16_t(v | ~mask);
        } else {
            rand_seed_ = 1664525 * rand_seed_ + 1013904223;
            uint16_t v = uint16_t((rand_seed_ >> 16) | (rand_seed_ << 16));
            if (random_hook) v = random_hook(v);
            return v;
        }
    default: return 0xffff;
    }
}

void System32::write16(uint32_t a, uint16_t data, uint16_t mask) {
    a &= 0xfffffe;
    switch (a >> 20) {
    case 0x2: {
        const uint32_t o = a & 0xffff;
        if (mask & 0x00ff) work_[o] = uint8_t(data);
        if (mask & 0xff00) work_[o + 1] = uint8_t(data >> 8);
        // SegaSonic's protection (rev. C): a write to the cleared-levels word
        // picks the next level from the table in ROM.
        if (a == 0x20e5c4) { // MAME installs the handler at this address only
            // segas32_m.cpp sonic_level_load_protection
            constexpr uint32_t CLEARED_LEVELS = 0xe5c4, CURRENT_LEVEL = 0xf06e, CURRENT_LEVEL_STATUS = 0xf0bc,
                               LEVEL_ORDER_ARRAY = 0x263a;
            const uint16_t cleared = uint16_t(work_[CLEARED_LEVELS] | work_[CLEARED_LEVELS + 1] << 8);
            uint16_t level;
            if (cleared == 0) level = 0x0007;
            else {
                const uint8_t *rom = img_.main_cpu.data() + LEVEL_ORDER_ARRAY;
                level = uint16_t(rom[cleared * 2 - 1] | rom[cleared * 2 - 2] << 8);
            }
            work_[CURRENT_LEVEL] = uint8_t(level);
            work_[CURRENT_LEVEL + 1] = uint8_t(level >> 8);
            for (int i = 0; i < 4; ++i) work_[CURRENT_LEVEL_STATUS + i] = 0;
        }
        break;
    }
    case 0x3:
        combine(videoram_[(a & 0x1ffff) >> 1], data, mask);
        video_->videoram_written((a & 0x1ffff) >> 1);
        break;
    case 0x4: combine(spriteram_[(a & 0x1ffff) >> 1], data, mask); break;
    case 0x5:
        if (mask & 0x00ff) sprite_control_[(a >> 1) & 7] = uint8_t(data);
        break;
    case 0x6:
        if (a & 0x10000) {
            if ((a & 0xff80) == 0) combine(mixer_[(a & 0x7f) >> 1], data, mask);
        } else palette_w((a & 0xffff) >> 1, data, mask);
        break;
    case 0x7: {
        const uint32_t o = a & 0x1fff;
        if (mask & 0x00ff) shared_[o] = uint8_t(data);
        if (mask & 0xff00) shared_[o + 1] = uint8_t(data >> 8);
        break;
    }
    case 0x8:
        if (!(mask & 0x00ff)) break;
        if (a < 0x801000) comm_[(a & 0xfff) >> 1] = uint8_t(data);
        else if (a == 0x801000) comm_cn_ = data & 1;
        else if (a == 0x801002 && comm_cn_) comm_fg_ = data & 1;
        break;
    case 0xc: {
        const uint32_t o = a & 0x7f;
        if (!(mask & 0x00ff)) break;
        if (o < 0x20) io_write(int(o >> 1), uint8_t(data));
        else if (o >= 0x40 && o < 0x58) upd_[(o - 0x40) >> 3].reset_xy();
        break;
    }
    case 0xd:
        if (a < 0xd80000)
            each_lane(a, mask, [&](uint32_t b, int lane) { int_control_w(int(b & 15), uint8_t(data >> (8 * lane))); });
        break;
    default: break;
    }
}

} // namespace segas32
