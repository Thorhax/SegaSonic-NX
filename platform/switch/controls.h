#pragma once
#include <switch.h>
#include <cstdint>

namespace switch_app {

enum Button : uint32_t {
    A = 1u << 0,
    B = 1u << 1,
    X = 1u << 2,
    Y = 1u << 3,
    Up = 1u << 4,
    Down = 1u << 5,
    Left = 1u << 6,
    Right = 1u << 7,
    L = 1u << 8,
    R = 1u << 9,
    ZL = 1u << 10,
    ZR = 1u << 11,
    Plus = 1u << 12,
    Minus = 1u << 13,
    StickL = 1u << 14,
    StickR = 1u << 15,
};

struct Pad {
    uint32_t buttons = 0;
    float lx = 0.0f, ly = 0.0f; // -1.0 to 1.0
    float rx = 0.0f, ry = 0.0f; // -1.0 to 1.0
};

inline bool menu_chord(uint32_t buttons) {
    return (buttons & (Plus | Minus)) == (Plus | Minus);
}

// Up to three players (the cabinet has three trackballs).
class Pads {
public:
    static constexpr int kPlayers = 3;

    void init() {
        padConfigureInput(kPlayers, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad_[0]);
        padInitialize(&pad_[1], HidNpadIdType_No2);
        padInitialize(&pad_[2], HidNpadIdType_No3);
    }

    Pad read(int idx) {
        padUpdate(&pad_[idx]);
        const u64 held = padGetButtons(&pad_[idx]);
        const HidAnalogStickState left = padGetStickPos(&pad_[idx], 0);
        const HidAnalogStickState right = padGetStickPos(&pad_[idx], 1);
        Pad pad{};
        pad.lx = float(left.x) / 32767.0f;
        pad.ly = float(left.y) / 32767.0f;
        pad.rx = float(right.x) / 32767.0f;
        pad.ry = float(right.y) / 32767.0f;
        static constexpr struct { u64 hid; uint32_t ours; } kMap[] = {
            {HidNpadButton_A, A}, {HidNpadButton_B, B}, {HidNpadButton_X, X}, {HidNpadButton_Y, Y},
            {HidNpadButton_Up, Up}, {HidNpadButton_Down, Down}, {HidNpadButton_Left, Left},
            {HidNpadButton_Right, Right}, {HidNpadButton_L, L}, {HidNpadButton_R, R},
            {HidNpadButton_ZL, ZL}, {HidNpadButton_ZR, ZR}, {HidNpadButton_Plus, Plus},
            {HidNpadButton_Minus, Minus}, {HidNpadButton_StickL, StickL}, {HidNpadButton_StickR, StickR},
        };
        for (const auto &m : kMap)
            if (held & m.hid) pad.buttons |= m.ours;
        return pad;
    }

private:
    PadState pad_[kPlayers];
};

} // namespace switch_app
