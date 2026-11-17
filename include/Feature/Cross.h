#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nq/cross.hpp>

#include <Defaults.h>

namespace nq {

class CrossOperator {
public:
    virtual ~CrossOperator() = default;
    virtual void Apply(const CrossView& view) = 0;
};

struct CrossSpec {
    std::string name;
    std::int64_t every = defaults::kSamplerPeriodNs;
    bool fill_gaps = false;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<std::unique_ptr<CrossOperator>()> make;
};

}  // namespace nq
