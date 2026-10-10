#include "board/rf5c68.h"

#include <algorithm>

namespace segas32 {

void Rf5c68::write(uint16_t offset, uint8_t data) {
    if (offset & 0x1000) ram_[uint16_t(wbank_ | (offset & 0x0fff))] = data;
    else if ((offset & 0x0f) <= 8) reg_w(offset & 0x0f, data); // 0x0000-0x0008, mirrored every 0x10
}

uint8_t Rf5c68::read(uint16_t offset) const {
    if (offset & 0x1000) return ram_[uint16_t(wbank_ | (offset & 0x0fff))];
    return 0x00; // write-only registers: unmapped in the Z80 space (MAME unmap value 0)
}

void Rf5c68::reg_w(int offset, uint8_t data) {
    Channel &chan = chan_[cbank_];
    switch (offset) {
    case 0x00: chan.env = data; break;
    case 0x01: chan.pan = data; break;
    case 0x02: chan.step = uint16_t((chan.step & 0xff00) | data); break;
    case 0x03: chan.step = uint16_t((chan.step & 0x00ff) | (data << 8)); break;
    case 0x04: chan.loopst = uint16_t((chan.loopst & 0xff00) | data); break;
    case 0x05: chan.loopst = uint16_t((chan.loopst & 0x00ff) | (data << 8)); break;
    case 0x06:
        chan.start = data;
        if (!chan.enable) chan.addr = uint32_t(chan.start) << (8 + 11);
        break;
    case 0x07:
        enable_ = (data >> 7) & 1;
        if (data & 0x40) cbank_ = data & 7;
        else wbank_ = uint16_t((data & 0xf) << 12);
        break;
    case 0x08:
        for (int i = 0; i < 8; i++) {
            chan_[i].enable = (~data >> i) & 1;
            if (!chan_[i].enable) chan_[i].addr = uint32_t(chan_[i].start) << (8 + 11);
        }
        break;
    }
}

void Rf5c68::generate(int n, std::vector<int16_t> &out) {
    const size_t base = out.size();
    out.resize(base + size_t(n) * 2, 0);
    if (!enable_ || n <= 0) return; // MAME leaves the stream silent
    left_.assign(size_t(n), 0);
    right_.assign(size_t(n), 0);
    for (Channel &chan : chan_) {
        if (!chan.enable) continue;
        const int lv = (chan.pan & 0x0f) * chan.env;
        const int rv = ((chan.pan >> 4) & 0x0f) * chan.env;
        for (int j = 0; j < n; j++) {
            int sample = ram_[(chan.addr >> 11) & 0xffff];
            if (sample == 0xff) {
                chan.addr = uint32_t(chan.loopst) << 11;
                sample = ram_[(chan.addr >> 11) & 0xffff];
                if (sample == 0xff) break; // looping onto a loop point: dead
            }
            chan.addr += chan.step;
            if (sample & 0x80) {
                sample &= 0x7f;
                left_[size_t(j)] += (sample * lv) >> 5;
                right_[size_t(j)] += (sample * rv) >> 5;
            } else {
                left_[size_t(j)] -= (sample * lv) >> 5;
                right_[size_t(j)] -= (sample * rv) >> 5;
            }
        }
    }
    // 10-bit output: drop the low 6 bits, clamp to 16 bits
    for (int j = 0; j < n; j++) {
        out[base + size_t(j) * 2 + 0] = int16_t(std::clamp(left_[size_t(j)] & ~0x3f, -32768, 32767));
        out[base + size_t(j) * 2 + 1] = int16_t(std::clamp(right_[size_t(j)] & ~0x3f, -32768, 32767));
    }
}

} // namespace segas32
