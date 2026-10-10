// Stand-ins for the few MAME facilities the V60 core uses, so its source
// stays as upstream: integer types, error reporting, float bit casts, and an
// `address_space` with MAME's accessor names on top of our 16-bit bus.
#pragma once

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using offs_t = uint32_t;

enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };
enum { INPUT_LINE_IRQ0 = 0, INPUT_LINE_NMI = 32 };

// MAME's two-argument BIT (libnx defines a one-argument one; ours wins in
// the files that include this, which use only MAME's).
#undef BIT
#define BIT(x, n) (((x) >> (n)) & 1)

[[noreturn]] inline void fatalerror(const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    throw std::runtime_error(std::string("V60: ") + buf);
}
#define logerror(...) ((void)0)
#define osd_printf_debug(...) ((void)0)

// osd/eminline.h and lib/util/coretmpl.h helpers (MAME's portable versions).
constexpr int64_t mul_32x32(int32_t a, int32_t b) { return int64_t(a) * int64_t(b); }
constexpr uint64_t mulu_32x32(uint32_t a, uint32_t b) { return uint64_t(a) * uint64_t(b); }
inline int32_t div_64x32_rem(int64_t a, int32_t b, int32_t &remainder) {
    const int32_t res = int32_t(a / int64_t(b));
    remainder = int32_t(a - int64_t(b) * res);
    return res;
}
inline uint32_t divu_64x32_rem(uint64_t a, uint32_t b, uint32_t &remainder) {
    const uint32_t res = uint32_t(a / uint64_t(b));
    remainder = uint32_t(a - uint64_t(b) * res);
    return res;
}
constexpr uint32_t swapendian_int32(uint32_t v) {
    return (v << 24) | ((v << 8) & 0x00ff0000) | ((v >> 8) & 0x0000ff00) | (v >> 24);
}
// bitswap<N>(val, b_{N-1}, ..., b0): result bit i = val bit b_i.
template <int N, typename T, typename... B>
constexpr T bitswap(T val, B... bits) {
    static_assert(sizeof...(B) == N);
    const int list[] = {int(bits)...};
    T r = 0;
    for (int i = 0; i < N; ++i) r = T((r << 1) | ((val >> list[i]) & 1));
    return r;
}

inline float u2f(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
inline uint32_t f2u(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

namespace segas32 {

// The board's side of a 16-bit little-endian bus (as MAME's address space
// with data width 16): word accesses at even addresses with a byte mask.
class Bus16 {
public:
    virtual ~Bus16() = default;
    virtual uint16_t read16(uint32_t addr, uint16_t mask) = 0;
    virtual void write16(uint32_t addr, uint16_t data, uint16_t mask) = 0;
};

} // namespace segas32

// MAME's accessor names over a Bus16, addresses masked to the CPU's bus
// width; bytes and unaligned or wider accesses split as MAME's memory system
// splits them on a 16-bit little-endian bus.
class address_space {
public:
    void attach(segas32::Bus16 *bus, uint32_t addr_mask) {
        bus_ = bus;
        mask_ = addr_mask;
    }

    u8 read_byte(offs_t a) {
        a &= mask_;
        const u16 w = bus_->read16(a & ~1u, (a & 1) ? 0xff00 : 0x00ff);
        return u8((a & 1) ? w >> 8 : w);
    }
    u16 read_word(offs_t a) { return bus_->read16(a & mask_ & ~1u, 0xffff); }
    u16 read_word_unaligned(offs_t a) {
        if (!(a & 1)) return read_word(a);
        return u16(read_byte(a) | read_byte(a + 1) << 8);
    }
    u32 read_dword(offs_t a) { return u32(read_word(a)) | u32(read_word(a + 2)) << 16; }
    u32 read_dword_unaligned(offs_t a) {
        return u32(read_word_unaligned(a)) | u32(read_word_unaligned(a + 2)) << 16;
    }
    u64 read_qword_unaligned(offs_t a) {
        return u64(read_dword_unaligned(a)) | u64(read_dword_unaligned(a + 4)) << 32;
    }

    void write_byte(offs_t a, u8 v) {
        a &= mask_;
        if (a & 1) bus_->write16(a & ~1u, u16(v << 8), 0xff00);
        else bus_->write16(a, v, 0x00ff);
    }
    void write_word(offs_t a, u16 v) { bus_->write16(a & mask_ & ~1u, v, 0xffff); }
    void write_word_unaligned(offs_t a, u16 v) {
        if (!(a & 1)) return write_word(a, v);
        write_byte(a, u8(v));
        write_byte(a + 1, u8(v >> 8));
    }
    void write_dword_unaligned(offs_t a, u32 v) {
        write_word_unaligned(a, u16(v));
        write_word_unaligned(a + 2, u16(v >> 16));
    }
    void write_qword_unaligned(offs_t a, u64 v) {
        write_dword_unaligned(a, u32(v));
        write_dword_unaligned(a + 4, u32(v >> 32));
    }

private:
    segas32::Bus16 *bus_ = nullptr;
    uint32_t mask_ = 0xffffff;
};
