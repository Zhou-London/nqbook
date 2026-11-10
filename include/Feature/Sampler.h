#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nq/feature_abi.h>

#include <Defaults.h>

#include <Feature/RowBuffer.h>
#include <Feature/Trigger.h>
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

    std::int64_t Due() const noexcept {
        if (const auto* bar = std::get_if<BarTrigger>(&trigger_)) return bar->Due();
        return std::get_if<CountTrigger>(&trigger_)->Due();
    }

    template <typename Reader>
    void OnEvent(std::int64_t ts, std::uint8_t action, Reader&& read) {
        Visit([&](auto& t) { t.OnEvent(ts, action, Emitter(read)); });
    }

    template <typename Reader>
    void Advance(std::int64_t ts, Reader&& read) {
        Visit([&](auto& t) { t.Advance(ts, Emitter(read)); });
    }

    template <typename Reader>
    void Close(Reader&& read) {
        Visit([&](auto& t) { t.Close(Emitter(read)); });
    }

    void ResetBaseline() noexcept;

private:
    template <typename Fn>
    void Visit(Fn&& fn) {
        if (auto* bar = std::get_if<BarTrigger>(&trigger_)) fn(*bar);
        else fn(*std::get_if<CountTrigger>(&trigger_));
    }

    template <typename Reader>
    auto Emitter(Reader& read) {
        return [this, &read](std::int64_t at) { Emit(at, read()); };
    }

    void Emit(std::int64_t ts, std::span<const double> values);

    SamplerSpec spec_;
    AnyTrigger trigger_;
    std::vector<SamplerColumn> columns_;
    RowBuffer rows_;
    std::vector<double> last_;
    std::vector<double> row_;
    RowSink* sink_;
    std::uint32_t sampler_index_;
    std::uint32_t instrument_;
};

}  // namespace nq
