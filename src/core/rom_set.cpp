#include "core/rom_set.h"

#include "core/zip.h"

#include <cstring>
#include <map>

namespace segas32 {

namespace {

enum Region { MainCpu, SoundCpu, Tiles, Sprites };

// How a chip lands in its region (MAME romload): Plain copies the bytes;
// Byte16 is ROM_LOAD16_BYTE (every other byte); Word64 is ROM_LOAD64_WORD
// (two bytes, then skip six). `copies` reloads the chip `stride` bytes
// further on each time (the driver's _x2/_x4 macros).
enum Kind { Plain, Byte16, Word64 };

struct Load {
    const char *name;
    uint32_t crc, size;
    Region region;
    uint32_t offset;
    Kind kind;
    int copies;
    uint32_t stride;
};

// MAME 0.289 src/mame/sega/segas32.cpp, ROM_START( sonic ).
constexpr Load kSonic[] = {
    {"epr-15787c.ic17", 0x25e3c27e, 0x020000, MainCpu, 0x000000, Plain, 4, 0x020000},
    {"epr-15786c.ic8", 0xefe9524c, 0x020000, MainCpu, 0x080000, Plain, 4, 0x020000},
    {"epr-15781c.ic18", 0x65b06c25, 0x040000, MainCpu, 0x100000, Byte16, 2, 0x080000},
    {"epr-15780c.ic9", 0x2db66fd2, 0x040000, MainCpu, 0x100001, Byte16, 2, 0x080000},

    {"epr-15785.ic36", 0x0fe7422e, 0x040000, SoundCpu, 0x000000, Plain, 4, 0x040000},
    {"mpr-15784.ic35", 0x42f06714, 0x100000, SoundCpu, 0x100000, Plain, 1, 0},
    {"mpr-15783.ic34", 0xe4220eea, 0x100000, SoundCpu, 0x200000, Plain, 1, 0},
    {"mpr-15782.ic33", 0xcf56b5a0, 0x100000, SoundCpu, 0x300000, Plain, 1, 0},

    {"mpr-15789.ic14", 0x4378f12b, 0x100000, Tiles, 0x000000, Byte16, 1, 0},
    {"mpr-15788.ic5", 0xa6ed5d7a, 0x100000, Tiles, 0x000001, Byte16, 1, 0},

    {"mpr-15790.ic32", 0xc69d51b1, 0x200000, Sprites, 0x000000, Word64, 1, 0},
    {"mpr-15792.ic30", 0x1006bb67, 0x200000, Sprites, 0x000002, Word64, 1, 0},
    {"mpr-15794.ic28", 0x8672b480, 0x200000, Sprites, 0x000004, Word64, 1, 0},
    {"mpr-15796.ic26", 0x95b8011c, 0x200000, Sprites, 0x000006, Word64, 1, 0},
    // 16 Mbit chips dumped as 8 Mbit (their top halves are all 0xff).
    {"mpr-15791.ic31", 0x42217066, 0x100000, Sprites, 0x800000, Word64, 1, 0},
    {"mpr-15793.ic29", 0x75bafe55, 0x100000, Sprites, 0x800002, Word64, 1, 0},
    {"mpr-15795.ic27", 0x7f3dad30, 0x100000, Sprites, 0x800004, Word64, 1, 0},
    {"mpr-15797.ic25", 0x013c6cab, 0x100000, Sprites, 0x800006, Word64, 1, 0},
};

std::string base_name(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

void place(std::vector<uint8_t> &region, const Load &load, const std::vector<uint8_t> &chip) {
    for (int k = 0; k < load.copies; ++k) {
        const size_t base = load.offset + size_t(k) * load.stride;
        const size_t last = load.kind == Plain ? base + chip.size() - 1
                          : load.kind == Byte16 ? base + 2 * (chip.size() - 1)
                                                : base + 4 * (chip.size() - 2) + 1;
        if (last >= region.size()) throw RomError("ROM layout overflows its region");
        switch (load.kind) {
        case Plain:
            std::memcpy(region.data() + base, chip.data(), chip.size());
            break;
        case Byte16:
            for (size_t i = 0; i < chip.size(); ++i) region[base + 2 * i] = chip[i];
            break;
        case Word64:
            for (size_t i = 0; i + 1 < chip.size(); i += 2) {
                region[base + 4 * i] = chip[i];
                region[base + 4 * i + 1] = chip[i + 1];
            }
            break;
        }
    }
}

} // namespace

RomImages load_sonic(const std::string &zip_path) {
    Zip zip(zip_path);

    std::map<std::string, std::string> by_name; // lower-case base name -> entry
    std::map<uint32_t, std::string> by_crc;
    for (const auto &[name, entry] : zip.entries()) {
        by_name.emplace(lower(base_name(name)), name);
        by_crc.emplace(entry.crc, name);
    }

    RomImages img;
    img.main_cpu.assign(0x200000, 0);
    img.sound_cpu.assign(0x400000, 0);
    img.tiles.assign(0x200000, 0);
    img.sprites.assign(0x1000000, 0);

    std::string problems;
    for (const Load &load : kSonic) {
        std::string entry;
        if (auto it = by_name.find(load.name); it != by_name.end() && zip.entries().at(it->second).crc == load.crc)
            entry = it->second;
        else if (auto c = by_crc.find(load.crc); c != by_crc.end())
            entry = c->second;
        if (entry.empty()) {
            problems += std::string(by_name.count(load.name) ? " wrong: " : " missing: ") + load.name;
            continue;
        }
        const std::vector<uint8_t> chip = zip.read(entry);
        if (chip.size() != load.size) {
            problems += std::string(" wrong size: ") + load.name;
            continue;
        }
        std::vector<uint8_t> &region = load.region == MainCpu ? img.main_cpu
                                     : load.region == SoundCpu ? img.sound_cpu
                                     : load.region == Tiles ? img.tiles
                                                            : img.sprites;
        place(region, load, chip);
    }
    if (!problems.empty()) throw RomError("sonic.zip (SegaSonic, MAME set sonic):" + problems);
    return img;
}

} // namespace segas32
