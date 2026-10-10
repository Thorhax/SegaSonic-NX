#include "board/scheduler.h"

#include <algorithm>

namespace segas32 {

namespace {
constexpr Scheduler::atto kQuantum = Scheduler::kAttoPerSecond / 60; // attotime::from_hz(60)
}

Scheduler::atto Scheduler::time() const {
    if (callback_timer_) return callback_expire_; // inside a timer callback: that timer's expiry
    if (executing_)
        return executing_->localtime + atto(uint64_t(executing_->cycles_running - executing_->icount())) * executing_->apc;
    return basetime_;
}

void Scheduler::insert(Timer &t) {
    if (t.expire == kNever || !t.enabled) return; // inactive
    auto it = std::find_if(list_.begin(), list_.end(), [&](Timer *c) { return c->expire > t.expire; });
    list_.insert(it, &t);
    t.in_list = true;
}

void Scheduler::remove(Timer &t) {
    if (!t.in_list) return;
    list_.erase(std::find(list_.begin(), list_.end(), &t));
    t.in_list = false;
}

void Scheduler::abort_timeslice_of(Exec &e) {
    if (executing_ != &e) return;
    const int left = e.icount();
    if (left > 0) {
        e.cycles_stolen += left;
        e.cycles_running -= left;
        e.zero_icount();
    }
}

void Scheduler::abort_timeslice() {
    if (executing_) abort_timeslice_of(*executing_);
}

void Scheduler::adjust(Timer &t, atto delay, int param, atto period) {
    if (callback_timer_ == &t) callback_timer_modified_ = true;
    t.param = param;
    t.enabled = true;
    t.start = time();
    t.expire = t.start + delay;
    t.period = period;
    remove(t);
    insert(t);
    if (!list_.empty() && list_.front() == &t) abort_timeslice();
}

void Scheduler::enable(Timer &t, bool on) {
    t.enabled = on;
    remove(t);
    if (on) insert(t);
}

void Scheduler::synchronize(std::function<void(int)> cb, int param) {
    temps_.emplace_back();
    Timer &t = temps_.back();
    t.callback = std::move(cb);
    t.param = param;
    t.temporary = true;
    t.enabled = true;
    t.start = time();
    t.expire = t.start;
    insert(t);
    if (!list_.empty() && list_.front() == &t) abort_timeslice();
}

void Scheduler::apply_suspend_changes() {
    for (Exec *e : execs_) e->suspended = e->next_suspended;
    suspend_changes_pending_ = false;
}

void Scheduler::timeslice() {
    while (!list_.empty() && basetime_ < list_.front()->expire) {
        atto target = basetime_ + kQuantum;
        if (list_.front()->expire < target) target = list_.front()->expire;
        if (suspend_changes_pending_) apply_suspend_changes();

        for (Exec *e : execs_) {
            if (target <= e->localtime) continue; // MAME: delta < one cycle (or negative)
            const atto delta = target - e->localtime;
            if (delta < e->apc) continue;
            int ran = e->cycles_running = int(uint64_t(delta / e->apc));
            if (!e->suspended) {
                e->cycles_stolen = 0;
                executing_ = e;
                e->run(e->cycles_running);
                ran -= e->icount();
                ran -= e->cycles_stolen;
            }
            e->total_cycles += uint64_t(ran);
            e->localtime += atto(uint64_t(ran)) * e->apc;
            if (e->localtime < target) target = std::max(e->localtime, basetime_);
        }
        executing_ = nullptr;
        basetime_ = target;
    }
    execute_timers();
}

void Scheduler::execute_timers() {
    while (!list_.empty() && list_.front()->expire <= basetime_) {
        Timer &t = *list_.front();
        const bool was_enabled = t.enabled;
        if (t.period == 0) t.enabled = false;
        callback_timer_modified_ = false;
        callback_timer_ = &t;
        callback_expire_ = t.expire;
        if (was_enabled && t.callback) t.callback(t.param);
        callback_timer_ = nullptr;
        if (!callback_timer_modified_) {
            if (t.temporary) {
                remove(t);
                temps_.remove_if([&](const Timer &x) { return &x == &t; });
            } else {
                // schedule_next_period
                t.start = t.expire;
                t.expire = t.period ? t.expire + t.period : kNever;
                remove(t);
                insert(t);
            }
        }
    }
}

} // namespace segas32
