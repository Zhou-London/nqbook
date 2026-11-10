#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nq::defaults {

inline constexpr std::size_t kExpectedOrders = 1024;
inline constexpr std::size_t kInstrumentSlots = 8192;
inline constexpr std::size_t kQueueCapacity = 1 << 16;
inline constexpr std::size_t kConsumeBatch = 4096;
inline constexpr std::int64_t kRowGroupRows = 65536;
inline constexpr std::int64_t kSamplerPeriodNs = 1'000'000'000;
inline constexpr std::string_view kOutDir = "out";

}  // namespace nq::defaults
