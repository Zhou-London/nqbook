#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nlib/common.h>
#include <nq/abi.h>

#include <Feature/RowBuffer.h>

namespace nq {

enum class Trigger : std::uint8_t { Time = 0, Events = 1, Trades = 2 };

struct SamplerSpec {
    std::string name;
    Trigger trigger = Trigger::Time;
    std::int64_t every = 1'000'000'000;
    bool fill_gaps = false;
    std::vector<std::string> columns;

    static SamplerSpec Time(std::string name, std::int64_t interval_ns) {
        return SamplerSpec{std::move(name), Trigger::Time, interval_ns, false, {}};
    }
    static SamplerSpec Events(std::string name, std::int64_t count) {
        return SamplerSpec{std::move(name), Trigger::Events, count, false, {}};
    }
    static SamplerSpec Trades(std::string name, std::int64_t count) {
        return SamplerSpec{std::move(name), Trigger::Trades, count, false, {}};
    }

    SamplerSpec&& FillGaps(bool on = true) && {
        fill_gaps = on;
        return std::move(*this);
    }
    SamplerSpec&& Columns(std::vector<std::string> names) && {
        columns = std::move(names);
        return std::move(*this);
    }
};

struct SamplerColumn {
    std::size_t index;
    std::uint8_t kind;
};

class Sampler {
public:
    Sampler(SamplerSpec spec, std::vector<std::string> names, std::vector<SamplerColumn> columns);

    const SamplerSpec& Spec() const noexcept { return spec_; }
    const RowBuffer& Rows() const noexcept { return rows_; }
    RowBuffer& Rows() noexcept { return rows_; }
    std::size_t Ncol() const noexcept { return columns_.size(); }

    template <typename Reader>
    void Advance(std::int64_t ts, Reader&& read) {
        if (spec_.trigger != Trigger::Time) return;
        const std::int64_t interval = spec_.every;
        if (!started_) {
            started_ = true;
            next_ = (ts / interval + 1) * interval;
            return;
        }
        if (ts < next_) return;
        const std::span<const double> values = read();
        if (spec_.fill_gaps) {
            while (ts >= next_) {
                Emit(next_, values);
                next_ += interval;
            }
            return;
        }
        Emit(next_, values);
        next_ = (ts / interval + 1) * interval;
    }

    bool OnEvent(nlib::action a) noexcept;

    void Emit(std::int64_t ts, std::span<const double> values);
    void ResetBaseline() noexcept;

private:
    SamplerSpec spec_;
    std::vector<SamplerColumn> columns_;
    RowBuffer rows_;
    std::vector<double> last_;
    std::vector<double> row_;
    std::int64_t next_ = 0;
    bool started_ = false;
    std::int64_t count_ = 0;
};

}  // namespace nq
