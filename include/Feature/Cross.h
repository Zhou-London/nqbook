#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nq/cross.hpp>

#include <Defaults.h>
#include <Feature/BarClock.h>

namespace nq {

class CrossOperator {
public:
    virtual ~CrossOperator() = default;
    virtual void Apply(const CrossView& view) = 0;
};

struct CrossSpec {
    std::string name;
    TimeBars bars{defaults::kSamplerPeriodNs};
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<std::unique_ptr<CrossOperator>()> make;
};

}  // namespace nq
