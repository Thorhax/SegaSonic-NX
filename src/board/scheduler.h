// MAME's device scheduler (src/emu/schedule.cpp, diexec.cpp; BSD-3-Clause),
// as far as this board needs it, so the two CPUs interleave and interrupts
// land exactly as in MAME:
// - timers in one list ordered by expiry (later insertions after equal
//   ones); a timer that becomes the head while a CPU runs aborts that CPU's
//   slice (abort_timeslice: the unused cycles leave the slice);
// - timeslices up to min(base + 1/60 s, next timer); each CPU in order runs
//   floor((target - local time) / cycle) cycles; a CPU that stops early pulls
//   the target back for the CPUs after it; then due timers fire;
// - a CPU held in reset is suspended and eats its cycles (keeps time);
// - input lines change through a queue and a zero-delay timer (synchronize);
// - inside a timer callback, time() is that timer's expiry.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <vector>

namespace segas32 {

class Scheduler {
public:
    using atto = unsigned __int128;
    static constexpr uint64_t kAttoPerSecond = 1000000000000000000ull;
    static constexpr atto kNever = ~atto(0);

    struct Timer {
        atto start = 0, expire = kNever, period = 0; // period 0: one shot
        bool enabled = false, temporary = false, in_list = false;
        int param = 0;
        std::function<void(int)> callback;
    };

    struct Exec {
        uint64_t clock = 1, apc = 1; // Hz, HZ_TO_ATTOSECONDS
        atto localtime = 0;
        int cycles_running = 0, cycles_stolen = 0;
        bool suspended = false, next_suspended = false;
        uint64_t total_cycles = 0;
        std::function<void(int)> run;    // execute for n cycles (sets the CPU's icount)
        std::function<int()> icount;     // cycles left in the current run
        std::function<void()> zero_icount;
    };

    static uint64_t hz_to_atto(uint64_t hz) { return kAttoPerSecond / hz; }
    // attotime::from_ticks
    static atto from_ticks(uint64_t ticks, uint64_t freq) {
        const uint64_t apt = hz_to_atto(freq);
        if (ticks < freq) return atto(ticks) * apt;
        return atto(ticks / freq) * kAttoPerSecond + atto(ticks % freq) * apt;
    }

    void add_exec(Exec *e) { execs_.push_back(e); }
    void reset_time() {
        basetime_ = 0;
        for (Exec *e : execs_) e->localtime = 0;
    }

    atto time() const; // machine().time()
    bool executing() const { return executing_ != nullptr; }

    void adjust(Timer &t, atto delay, int param = 0, atto period = 0);
    void enable(Timer &t, bool on);
    void synchronize(std::function<void(int)> cb, int param = 0); // a temporary zero-delay timer

    void suspend(Exec &e, bool on) { // suspend/resume (reason RESET, eat cycles)
        e.next_suspended = on;
        suspend_changes_pending_ = true;
        abort_timeslice_of(e);
    }
    void timeslice();

    atto basetime() const { return basetime_; }

private:
    void insert(Timer &t);
    void remove(Timer &t);
    void abort_timeslice();
    void abort_timeslice_of(Exec &e);
    void execute_timers();
    void apply_suspend_changes();

    std::vector<Exec *> execs_;
    Exec *executing_ = nullptr;
    atto basetime_ = 0;
    std::vector<Timer *> list_; // ordered by expiry; [0] is the head
    std::list<Timer> temps_;    // storage for temporary timers
    Timer *callback_timer_ = nullptr;
    atto callback_expire_ = 0;
    bool callback_timer_modified_ = false;
    bool suspend_changes_pending_ = false;
};

} // namespace segas32
