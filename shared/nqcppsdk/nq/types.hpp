#pragma once

#include <cstdint>

#include <nq/abi.h>

namespace nq {

enum class Side : std::uint8_t { Bid = NQ_BID, Ask = NQ_ASK };

enum class Action : std::uint8_t {
    Add = NQ_ADD,
    Cancel = NQ_CANCEL,
    Modify = NQ_MODIFY,
    Trade = NQ_TRADE,
    Clear = NQ_CLEAR
};

enum class Feed : std::uint8_t { MBO = NQ_FEED_MBO, OHLCV = NQ_FEED_OHLCV };

using Ohlcv = nq_ohlcv;

constexpr Side Opposite(Side s) noexcept { return s == Side::Bid ? Side::Ask : Side::Bid; }

}  // namespace nq
