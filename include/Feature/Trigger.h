#pragma once

#include <cstdint>
#include <variant>

#include <Clock/TimerWheel.h>
#include <Feature/BarClock.h>

namespace nq {

class BarTrigger {
public:
    explicit BarTrigger(TimeBars bars) noexcept : clock_(bars) {}

    template <typename Emit>
    void OnEvent(std::int64_t, bool, Emit&&) noexcept {
        dirty_ = true;
    }

    template <typename Emit>
    void Advance(std::int64_t ts, Emit&& emit) {
        clock_.Advance(ts, dirty_, [&](std::int64_t at) { Fire(at, emit); });
    }

    template <typename Emit>
    void Close(Emit&& emit) {
        if (clock_.Armed(dirty_)) Fire(clock_.Next(), emit);
    }

    std::int64_t Due() const noexcept { return clock_.Armed(dirty_) ? clock_.Next() : TimerWheel::kNever; }

private:
    template <typename Emit>
    void Fire(std::int64_t at, Emit& emit) {
        dirty_ = false;
        emit(at);
    }

    BarClock clock_;
    bool dirty_ = false;
};

class CountTrigger {
public:
    CountTrigger(std::int64_t every, bool trades_only) noexcept : every_(every), trades_only_(trades_only) {}

    template <typename Emit>
    void OnEvent(std::int64_t ts, bool is_trade, Emit&& emit) {
        last_ts_ = ts;
        if (trades_only_ && !is_trade) return;
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

    std::int64_t Due() const noexcept { return TimerWheel::kNever; }

private:
    std::int64_t every_;
    bool trades_only_;
    std::int64_t count_ = 0;
    std::int64_t last_ts_ = 0;
};

using AnyTrigger = std::variant<BarTrigger, CountTrigger>;

}  // namespace nq
