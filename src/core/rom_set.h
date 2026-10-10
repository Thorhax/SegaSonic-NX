// The user's ROM set (MAME `sonic`: SegaSonic the Hedgehog, Japan, rev. C,
// Sega System 32 with the trackball I/O board) unpacked into the memory
// regions MAME's segas32.cpp builds, byte for byte, so the board code can
// follow MAME's driver directly.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace segas32 {

struct RomError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RomImages {
    std::vector<uint8_t> main_cpu;  // V60 code + data, 0x200000
    std::vector<uint8_t> sound_cpu; // Z80 program + RF5C68 samples, 0x400000
    std::vector<uint8_t> tiles;     // 0x200000
    std::vector<uint8_t> sprites;   // 0x1000000 (MAME REGION32_BE layout)
};

// Reads `zip_path` and checks every chip's CRC. Files are matched by name
// (any folder inside the zip) or, failing that, by CRC. Throws RomError
// naming every missing or wrong chip.
RomImages load_sonic(const std::string &zip_path);

} // namespace segas32
