#pragma once

#include <cstdint>
#include <vector>

namespace nq {

struct FeatureRow {
    std::uint32_t sampler = 0;
    std::uint32_t instrument = 0;
    std::int64_t ts = 0;
    std::vector<double> values;
};

class RowSink {
public:
    virtual ~RowSink() = default;
    virtual void Push(FeatureRow row) = 0;
};

}  // namespace nq
