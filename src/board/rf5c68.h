// Ricoh RF5C68 PCM, after MAME 0.289 src/devices/sound/rf5c68.cpp
// (BSD-3-Clause): 8 channels over 64 KB of wave RAM, 10-bit output.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace segas32 {

class Rf5c68 {
public:
    Rf5c68() { ram_.fill(0xff); } // segas32_v.cpp: sound RAM starts at 0xff

    // System 32 Z80 window at 0xc000-0xdfff (MAME rf5c68_device::map, with
    // the Sega-board mirror): registers at A12 = 0, wave RAM at A12 = 1.
    void write(uint16_t offset, uint8_t data); // offset within the window
    uint8_t read(uint16_t offset) const;       // wave RAM; the registers are write-only

    // Appends `n` stereo samples (left, right interleaved, 16-bit range) at
    // the chip's rate (clock / 384).
    void generate(int n, std::vector<int16_t> &out);

private:
    struct Channel {
        uint8_t enable = 0, env = 0, pan = 0, start = 0;
        uint32_t addr = 0;
        uint16_t step = 0, loopst = 0;
    };
    void reg_w(int offset, uint8_t data);
    std::array<Channel, 8> chan_{};
    std::array<uint8_t, 0x10000> ram_;
    uint8_t cbank_ = 0, enable_ = 0;
    uint16_t wbank_ = 0;
    std::vector<int32_t> left_, right_;
};

} // namespace segas32
