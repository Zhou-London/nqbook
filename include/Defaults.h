#pragma once

#include <cstddef>
#include <cstdint>

namespace nq::defaults {

inline constexpr std::size_t kExpectedOrders = 1024;
inline constexpr std::size_t kInstrumentSlots = 8192;
inline constexpr std::size_t kQueueCapacity = 1 << 16;
inline constexpr std::size_t kConsumeBatch = 4096;
inline constexpr std::int64_t kSamplerPeriodNs = 1'000'000'000;

}  // namespace nq::defaults
