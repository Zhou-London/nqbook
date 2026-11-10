#pragma once

#include <cstdint>
#include <variant>

#include <nq/abi.h>

#include <Clock/TimerQueue.h>

namespace nq {

class BarTrigger {
public:
    BarTrigger(std::int64_t interval, bool fill_gaps) noexcept : interval_(interval), fill_gaps_(fill_gaps) {}

    template <typename Emit>
    void OnEvent(std::int64_t, std::uint8_t, Emit&&) noexcept {
        dirty_ = true;
    }

    template <typename Emit>
    void Advance(std::int64_t ts, Emit&& emit) {
        if (!started_) {
            started_ = true;
            next_ = BarAfter(ts);
            return;
        }
        if (ts < next_) return;
        if (fill_gaps_) {
            while (ts >= next_) {
                Fire(next_, emit);
                next_ += interval_;
            }
            return;
        }
        if (dirty_) Fire(next_, emit);
        next_ = BarAfter(ts);
    }

    template <typename Emit>
    void Close(Emit&& emit) {
        if (started_ && (dirty_ || fill_gaps_)) Fire(next_, emit);
    }

    std::int64_t Due() const noexcept {
        return started_ && (dirty_ || fill_gaps_) ? next_ : TimerQueue::kNever;
    }

private:
    std::int64_t BarAfter(std::int64_t ts) const noexcept { return (ts / interval_ + 1) * interval_; }

    template <typename Emit>
    void Fire(std::int64_t at, Emit& emit) {
        dirty_ = false;
        emit(at);
    }

    std::int64_t interval_;
    bool fill_gaps_;
    std::int64_t next_ = 0;
    bool started_ = false;
    bool dirty_ = false;
};

class CountTrigger {
public:
    CountTrigger(std::int64_t every, bool trades_only) noexcept : every_(every), trades_only_(trades_only) {}

    template <typename Emit>
    void OnEvent(std::int64_t ts, std::uint8_t action, Emit&& emit) {
        last_ts_ = ts;
        if (trades_only_ && action != NQ_TRADE) return;
        if (++count_ < every_) return;
        count_ = 0;
        emit(ts);
    }

    template <typename Emit>
    void Advance(std::int64_t, Emit&&) noexcept {}

    template <typename Emit>
    void Close(Emit&& emit) {
        if (count_ == 0) return;
        count_ = 0;
        emit(last_ts_);
    }

    std::int64_t Due() const noexcept { return TimerQueue::kNever; }

private:
    std::int64_t every_;
    bool trades_only_;
    std::int64_t count_ = 0;
    std::int64_t last_ts_ = 0;
};

using AnyTrigger = std::variant<BarTrigger, CountTrigger>;

}  // namespace nq
