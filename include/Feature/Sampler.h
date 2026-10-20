#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nq/feature_abi.h>

#include <Defaults.h>

#include <Feature/RowBuffer.h>
#include <Writer/RowSink.h>

namespace nq {

enum class Trigger : std::uint8_t { Time = 0, Events = 1, Trades = 2 };

struct SamplerSpec {
    std::string name;
    Trigger trigger = Trigger::Time;
    std::int64_t every = defaults::kSamplerPeriodNs;
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
    Sampler(SamplerSpec spec, std::vector<std::string> names, std::vector<SamplerColumn> columns,
            RowSink* sink = nullptr, std::uint32_t sampler_index = 0, std::uint32_t instrument = 0);

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

    template <typename Reader>
    void Close(Reader&& read) {
        if (spec_.trigger == Trigger::Time) {
            if (!started_ || !(dirty_ || spec_.fill_gaps)) return;
            Emit(next_, read());
            return;
        }
        if (count_ == 0) return;
        count_ = 0;
        Emit(last_ts_, read());
    }

    bool OnEvent(std::int64_t ts, std::uint8_t a) noexcept;

    void Emit(std::int64_t ts, std::span<const double> values);
    void ResetBaseline() noexcept;

private:
    SamplerSpec spec_;
    std::vector<SamplerColumn> columns_;
    RowBuffer rows_;
    std::vector<double> last_;
    std::vector<double> row_;
    RowSink* sink_;
    std::uint32_t sampler_index_;
    std::uint32_t instrument_;
    std::int64_t next_ = 0;
    bool started_ = false;
    bool dirty_ = false;
    std::int64_t last_ts_ = 0;
    std::int64_t count_ = 0;
};

}  // namespace nq
