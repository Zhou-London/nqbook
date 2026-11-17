#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nq/feature_abi.h>

#include <Feature/RowBuffer.h>
#include <Feature/Trigger.h>
#include <Writer/RowSink.h>

namespace nq {

struct EveryEvents {
    std::int64_t every;
};
struct EveryTrades {
    std::int64_t every;
};
using SamplerTrigger = std::variant<TimeBars, EveryEvents, EveryTrades>;

struct SamplerSpec {
    std::string name;
    SamplerTrigger trigger;
    std::vector<std::string> columns;

    static SamplerSpec Time(std::string name, std::int64_t every, bool fill_gaps = false) {
        return SamplerSpec{std::move(name), TimeBars{every, fill_gaps}, {}};
    }
    static SamplerSpec Events(std::string name, std::int64_t every) {
        return SamplerSpec{std::move(name), EveryEvents{every}, {}};
    }
    static SamplerSpec Trades(std::string name, std::int64_t every) {
        return SamplerSpec{std::move(name), EveryTrades{every}, {}};
    }

    std::int64_t Every() const noexcept {
        return std::visit([](const auto& t) { return t.every; }, trigger);
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
    template <typename Self, typename Fn>
    decltype(auto) Visit(this Self& self, Fn&& fn) {
        if (auto* bar = std::get_if<BarTrigger>(&self.trigger_)) return fn(*bar);
        return fn(*std::get_if<CountTrigger>(&self.trigger_));
    }

public:
    Sampler(const SamplerTrigger& trigger, std::vector<std::string> names, std::vector<SamplerColumn> columns,
            RowSink* sink, std::uint32_t table, std::uint32_t instrument);

    const RowBuffer& Rows() const noexcept { return rows_; }
    std::size_t Ncol() const noexcept { return sources_.size(); }

    std::int64_t Due() const noexcept {
        return Visit([](const auto& trigger) { return trigger.Due(); });
    }

    template <typename Reader>
    void OnEvent(std::int64_t ts, bool is_trade, Reader&& read) {
        Visit([&](auto& t) { t.OnEvent(ts, is_trade, Emitter(read)); });
    }

    template <typename Reader>
    void Advance(std::int64_t ts, Reader&& read) {
        Visit([&](auto& t) { t.Advance(ts, Emitter(read)); });
    }

    template <typename Reader>
    void Close(Reader&& read) {
        Visit([&](auto& t) { t.Close(Emitter(read)); });
    }

    void CarryDelta(std::span<const double> values) noexcept;
    void ResetBaseline(std::span<const double> values) noexcept;

private:
    struct DeltaColumn {
        std::size_t slot;
        double baseline = 0.0;
        double carry = 0.0;
    };

    template <typename Reader>
    auto Emitter(Reader& read) {
        return [this, &read](std::int64_t at) { Emit(at, read()); };
    }

    void Emit(std::int64_t ts, std::span<const double> values);

    AnyTrigger trigger_;
    std::vector<std::size_t> sources_;
    std::vector<DeltaColumn> deltas_;
    RowBuffer rows_;
    std::vector<double> row_;
    RowSink* sink_;
    std::uint32_t table_;
    std::uint32_t instrument_;
};

}  // namespace nq
