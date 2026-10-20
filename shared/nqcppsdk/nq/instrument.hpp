#pragma once

#include <cstdint>
#include <string>

namespace nq {

struct Instrument {
    std::uint32_t id = 0;
    std::string symbol;
    std::int64_t tick_size = 1;
    std::int64_t lot_size = 1;
};

}  // namespace nq
