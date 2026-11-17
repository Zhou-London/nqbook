#pragma once

#include <cstdint>
#include <limits>

namespace nq {

struct TimeBars {
    std::int64_t every;
    bool fill_gaps = false;
};

class BarClock {
public:
    explicit BarClock(TimeBars bars) noexcept : bars_(bars) {}

    bool FillsGaps() const noexcept { return bars_.fill_gaps; }
    std::int64_t Next() const noexcept { return next_; }
    bool Armed(bool pending) const noexcept { return Started() && (pending || bars_.fill_gaps); }

    template <typename Fire>
    void Advance(std::int64_t ts, bool pending, Fire&& fire) {
        if (ts < next_) return;
        if (!Started()) {
            next_ = BarAfter(ts);
            return;
        }
        if (bars_.fill_gaps) {
            while (ts >= next_) {
                fire(next_);
                next_ += bars_.every;
            }
            return;
        }
        if (pending) fire(next_);
        next_ = BarAfter(ts);
    }

private:
    static constexpr std::int64_t kNotStarted = std::numeric_limits<std::int64_t>::min();

    bool Started() const noexcept { return next_ != kNotStarted; }
    std::int64_t BarAfter(std::int64_t ts) const noexcept { return (ts / bars_.every + 1) * bars_.every; }

    TimeBars bars_;
    std::int64_t next_ = kNotStarted;
};

}  // namespace nq
