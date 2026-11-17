#pragma once

#include <cstdint>
#include <vector>

#include <nq/instrument.hpp>

namespace nq {

struct FeatureRow {
    std::uint32_t sampler = 0;
    std::uint32_t instrument = 0;
    std::int64_t ts = 0;
    std::vector<double> values;
};

struct FeatureBlock {
    std::uint32_t table = 0;
    std::int64_t ts = 0;
    std::vector<std::uint32_t> instruments;
    std::vector<double> values;
};

class RowSink {
public:
    virtual ~RowSink() = default;
    virtual void Register(const Instrument& instrument) = 0;
    virtual void Push(FeatureRow row) = 0;
    virtual void Push(FeatureBlock block) = 0;
    virtual FeatureBlock Acquire() { return {}; }
};

}  // namespace nq
