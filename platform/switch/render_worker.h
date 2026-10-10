// Draws frames on another core: the emulation thread hands over a video
// snapshot at VBLANK start (System32::snapshot_video) and goes on with the
// next frame while this thread renders it (one frame of display latency, as
// the VF2 port's threaded video). render_snapshot gives the same pixels as
// rendering on the emulation thread (checked on the host over 3,600 frames).
#pragma once
#include "board/system32.h"

#include <switch.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace switch_app {

class RenderWorker {
public:
    explicit RenderWorker(const segas32::RomImages &roms)
        : renderer_(std::make_unique<segas32::Video32>(roms.tiles, roms.sprites)) {
        thread_ = std::thread(&RenderWorker::loop, this);
    }
    ~RenderWorker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

    // Emulation thread, at VBLANK start: wait for the previous frame, copy
    // the snapshot, start drawing it.
    void submit(const segas32::System32 &board) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !busy_; });
        board.snapshot_video(snap_);
        busy_ = true;
        cv_.notify_all();
    }

    // Emulation thread: copies the newest finished frame; false if none new.
    bool take(std::vector<uint32_t> &pixels, int &width, uint64_t &render_ticks) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!fresh_) return false;
        pixels.swap(ready_);
        width = ready_width_;
        render_ticks = ready_ticks_;
        fresh_ = false;
        return true;
    }

private:
    void loop() {
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 0b0010);
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [this] { return busy_ || quit_; });
            if (quit_) return;
            lock.unlock();
            const uint64_t t0 = armGetSystemTick();
            const int width = renderer_->render_snapshot(snap_);
            const uint64_t ticks = armGetSystemTick() - t0;
            lock.lock();
            const auto &frame = renderer_->frame();
            ready_.assign(frame.begin(), frame.begin() + ptrdiff_t(size_t(width) * segas32::Video32::kHeight));
            ready_width_ = width;
            ready_ticks_ = ticks;
            fresh_ = true;
            busy_ = false;
            cv_.notify_all();
        }
    }

    std::unique_ptr<segas32::Video32> renderer_;
    segas32::Video32::Snapshot snap_;
    std::vector<uint32_t> ready_;
    int ready_width_ = 416;
    uint64_t ready_ticks_ = 0;
    bool busy_ = false, fresh_ = false, quit_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
};

} // namespace switch_app
