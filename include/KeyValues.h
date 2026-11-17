#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nq/abi.h>
#include <nq/params.hpp>

namespace nq {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

inline ParamsBuilder ScaledParams(const KeyValues& params) {
    ParamsBuilder pb;
    pb.PriceScale(NQ_PRICE_SCALE).QtyScale(NQ_QTY_SCALE);
    for (const auto& [k, v] : params) pb.Set(k, v);
    return pb;
}

}  // namespace nq
