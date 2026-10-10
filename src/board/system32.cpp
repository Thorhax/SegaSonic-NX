#include "board/system32.h"

#include <algorithm>
#include <cmath>

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

void Eeprom93c46::busy_for(double seconds) {
    if (clock) completion_ = clock() + uint64_t(std::llround(seconds * System32::kV60Clock));
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
                        busy_for(1750e-6);
                    } else {
                        data.fill(uint16_t(shift_)); // write all
                        busy_for(8000e-6);
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
            busy_for(1000e-6);
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
            busy_for(8000e-6);
            state_ = WaitDone;
        }
        break;
    }
}

// ---------------------------------------------------------------------------

System32::System32(RomImages images)
    : img_(std::move(images)), work_(0x10000), shared_(0x2000), comm_(0x800), videoram_(0x10000),
      spriteram_(0x10000), palette_(0x4000) {
    cpu_ = std::make_unique<v60_device>(*this);
    cpu_->irq_ack = [this] { return irq_callback(); };
    eeprom_.clock = [this] { return clock_now(); };
    reset();
}

void System32::reset() {
    std::fill(work_.begin(), work_.end(), 0);
    std::fill(shared_.begin(), shared_.end(), 0);
    std::fill(comm_.begin(), comm_.end(), 0);
    std::fill(videoram_.begin(), videoram_.end(), 0);
    std::fill(spriteram_.begin(), spriteram_.end(), 0);
    std::fill(palette_.begin(), palette_.end(), 0);
    mixer_.fill(0);
    sprite_control_.fill(0);
    sprite_control_latched_.fill(0);
    sprite_render_count_ = 0;
    sprite_buffer_b_ = false;
    std::fill(std::begin(irq_control_), std::end(irq_control_), 0xff); // device_reset
    std::fill(std::begin(io_latch_), std::end(io_latch_), 0);
    io_dir_ = io_cnt_ = 0;
    for (int i = 0; i < 8; ++i) io_out_port(i, 0);
    display_enable_ = 0;
    sound_reset_ = true;
    rand_seed_ = 0x9d14abd7;
    // MAME's first frame update reads the reversed X trackball ports as 0xfff
    // against a last value of 0, so each X counter starts at 0xfff (-1); the
    // game reads that once at boot and then resets the counters.
    for (auto &u : upd_) u = Upd4701{0x0fff, 0, 0, 0};
    events_.clear();
    vblank_due_ = false;
    now_ = 0;
    frame_ = 0;
    irq_line_ = false;
    cpu_->reset();
    cpu_->set_input(0, CLEAR_LINE);
    schedule(kFramePeriod, EvVblankStart);
}

void System32::schedule(atto when, int kind, int param) {
    cancel(kind);
    events_.push_back({when, kind, param});
    // Set from inside the CPU's slice and due before its end: stop the slice
    // after this instruction so the event fires on time.
    if (slice_len_ && uint64_t(when / kAttoPerCycle) < slice_target_) {
        // MAME's abort_timeslice: the unused cycles leave the slice, so the
        // CPU's local time is unchanged.
        const int left = cpu_->cycles_left();
        slice_len_ -= left;
        slice_target_ -= uint64_t(left);
        aborted_ = true;
        cpu_->abort_run();
    }
}

void System32::cancel(int kind) {
    events_.erase(std::remove_if(events_.begin(), events_.end(), [kind](const Event &e) { return e.kind == kind; }),
                  events_.end());
}

// MAME runs a CPU floor((target - local time) / cycle) clocks; an
// instruction starts while clocks remain.
void System32::run_until(atto when_as) {
    const uint64_t when = uint64_t(when_as / kAttoPerCycle);
    aborted_ = false;
    while (now_ < when && !aborted_) {
        slice_start_ = now_;
        slice_len_ = int(std::min<uint64_t>(when - now_, 1u << 20));
        slice_target_ = now_ + uint64_t(slice_len_);
        cpu_->run(slice_len_);
        now_ = slice_start_ + uint64_t(slice_len_ - cpu_->cycles_left()); // overshoot included
        slice_len_ = 0;
    }
}

void System32::run_frame(const Inputs &in) {
    in_ = in;
    for (int p = 0; p < 3; ++p) { // MAME counts trackball motion once per frame
        upd_[p].x = uint16_t((upd_[p].x + in.track_x[p]) & 0xffff);
        upd_[p].y = uint16_t((upd_[p].y + in.track_y[p]) & 0xffff);
    }
    for (;;) {
        auto next = std::min_element(events_.begin(), events_.end(),
                                     [](const Event &a, const Event &b) { return a.when < b.when; });
        const Event e = *next;
        run_until(e.when); // the CPU may reschedule timers meanwhile
        if (aborted_) continue; // stopped early for an event set meanwhile: pick again
        auto it = std::find_if(events_.begin(), events_.end(),
                               [&](const Event &x) { return x.kind == e.kind && x.when == e.when; });
        if (it == events_.end()) continue; // moved or cancelled while running
        if (e.kind == EvVblankStart && !vblank_due_) {
            // MAME reports the frame at VBLANK start before the interrupt
            // reaches the CPU (input lines are applied on the next sync):
            // stop here and deliver it when the next frame begins.
            vblank_due_ = true;
            break;
        }
        vblank_due_ = false;
        events_.erase(it);
        dispatch(e);
    }
    ++frame_;
}

void System32::dispatch(const Event &e) {
    switch (e.kind) {
    case EvVblankStart: // start_of_vblank_int
        signal_v60_irq(MAIN_IRQ_VBSTART);
        schedule(e.when + atto(kLines - kVblankLine) * kScanTime, EvVblankEnd); // time_until_pos(0)
        schedule(e.when + kFramePeriod, EvVblankStart);
        break;
    case EvVblankEnd: // end_of_vblank_int, then system32_set_vblank(0)
        signal_v60_irq(MAIN_IRQ_VBSTOP);
        schedule(e.when + atto(50) * 1000000000000ull, EvSprites); // 50 us
        break;
    case EvSprites:
        update_sprites();
        break;
    case EvTimer0:
    case EvTimer1:
        signal_v60_irq(e.param);
        break;
    }
}

// --- interrupt controller ---------------------------------------------------

void System32::update_irq_state() {
    const uint8_t effirq = irq_control_[7] & ~irq_control_[6] & 0x1f;
    cpu_->set_input(0, effirq ? ASSERT_LINE : CLEAR_LINE);
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
        // attotime::from_ticks(0x800 * duration, 32.2159 MHz / 2)
        if (duration) schedule(atto_now() + atto(0x800) * duration * kAttoPerCycle, EvTimer0, MAIN_IRQ_TIMER0);
        break;
    }
    case 10: case 11: {
        irq_control_[offset] = data;
        const uint16_t duration = uint16_t(irq_control_[10] | ((irq_control_[11] << 8) & 0xf00));
        // attotime::from_ticks(0x100 * duration, 50 MHz / 16)
        if (duration) schedule(atto_now() + atto(0x100) * duration * (kAttoPerSecond / 3125000), EvTimer1, MAIN_IRQ_TIMER1);
        break;
    }
    case 12: case 13: case 14: case 15:
        // signal_sound_irq(SOUND_IRQ_V60): the sound CPU comes with step 5
        break;
    }
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
        display_enable_ = BIT(data, 1);  // CNT1
        sound_reset_ = !BIT(data, 2);    // CNT2, inverted to the Z80's reset
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

// --- video-side registers ------------------------------------------------------

uint8_t System32::sprite_control_r(int offset) {
    switch (offset) {
    case 0: return uint8_t(0xfc | (sprite_buffer_b_ ? 0 : 1));
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
    // erase (bit 1) and render (bit 0) belong to the renderer (step 4); the
    // swap's side effects the CPU can see are kept here.
    if (BIT(sprite_control_[0], 0)) {
        sprite_buffer_b_ = !sprite_buffer_b_;
        sprite_control_latched_ = sprite_control_;
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
    case 0x3: combine(videoram_[(a & 0x1ffff) >> 1], data, mask); break;
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
