#pragma once

#include <cstdint>
#include <vector>

#include <nq/instrument.hpp>

namespace nq {

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
    virtual void Push(FeatureBlock block) = 0;
    virtual FeatureBlock Acquire(std::uint32_t) { return {}; }
};

}  // namespace nq
