// Audio output: 48 kHz stereo int16 from the board, played through an SDL
// callback from a ring buffer kept near kTargetMs. The emulation's frame
// clock and the audio clock drift slightly: when the buffer runs long or
// short, one sample in 256 is dropped or repeated.
#pragma once
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#include <switch.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace switch_app {

class Audio {
public:
    static constexpr int kRate = 48000;
    static constexpr int kTargetMs = 60;

    bool open() {
        SDL_AudioSpec want{}, got{};
        want.freq = kRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = &Audio::callback;
        want.userdata = this;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
        ring_.assign(size_t(kRate) * 2 * 2, 0); // 1 s of stereo frames
        return device_ != 0;
    }

    void close() {
        if (device_) SDL_CloseAudioDevice(device_);
        device_ = 0;
    }

    void push(const std::vector<int16_t> &pcm) {
        if (!device_) return;
        SDL_LockAudioDevice(device_);
        const size_t frames = pcm.size() / 2, cap = ring_.size() / 2;
        const size_t queued = count_;
        const size_t target = size_t(kRate) * kTargetMs / 1000;
        const int mode = queued > target * 2 ? 1 : (playing_ && queued < target / 2) ? -1 : 0; // drop / repeat
        for (size_t i = 0; i < frames; ++i) {
            if (mode == 1 && i % 256 == 128) continue;
            for (int rep = 0; rep < (mode == -1 && i % 256 == 128 ? 2 : 1); ++rep) {
                if (count_ == cap) break; // full: drop
                ring_[(head_ * 2) % ring_.size()] = pcm[i * 2];
                ring_[(head_ * 2 + 1) % ring_.size()] = pcm[i * 2 + 1];
                head_ = (head_ + 1) % cap;
                ++count_;
            }
        }
        const bool start = !playing_ && count_ >= target;
        SDL_UnlockAudioDevice(device_);
        if (start) {
            playing_ = true;
            SDL_PauseAudioDevice(device_, 0);
        }
    }

    void pause() {
        if (!device_) return;
        SDL_PauseAudioDevice(device_, 1);
        SDL_LockAudioDevice(device_);
        count_ = 0;
        playing_ = false;
        SDL_UnlockAudioDevice(device_);
    }

    void mute(bool on) { muted_ = on; }

    double queued_ms() const { return double(count_) * 1000.0 / kRate; }

private:
    static void callback(void *user, Uint8 *stream, int len) {
        auto *self = static_cast<Audio *>(user);
        static bool pinned = false;
        if (!pinned) { // keep the callback off the emulation core
            svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 0b0100);
            pinned = true;
        }
        auto *out = reinterpret_cast<int16_t *>(stream);
        const size_t frames = size_t(len) / 4, cap = self->ring_.size() / 2;
        for (size_t i = 0; i < frames; ++i) {
            int16_t l = 0, r = 0;
            if (self->count_) {
                const size_t tail = (self->head_ + cap - self->count_) % cap;
                l = self->ring_[tail * 2];
                r = self->ring_[tail * 2 + 1];
                --self->count_;
            }
            out[i * 2] = self->muted_ ? 0 : l;
            out[i * 2 + 1] = self->muted_ ? 0 : r;
        }
    }

    SDL_AudioDeviceID device_ = 0;
    std::vector<int16_t> ring_;
    size_t head_ = 0, count_ = 0;
    bool playing_ = false, muted_ = false;
};

} // namespace switch_app
