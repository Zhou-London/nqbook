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

enum class ControlType : std::uint8_t {
    Reset = NQ_CONTROL_RESET,
    SessionStart = NQ_CONTROL_SESSION_START,
    SessionEnd = NQ_CONTROL_SESSION_END
};

constexpr Side Opposite(Side s) noexcept { return s == Side::Bid ? Side::Ask : Side::Bid; }

struct Event {
    std::int64_t ts = 0;
    std::uint64_t id = 0;
    std::int64_t price = 0;
    std::int64_t qty = 0;
    std::int64_t filled = 0;
    Side side = Side::Bid;
    Action action = Action::Add;

    constexpr Event() = default;
    constexpr explicit Event(const nq_event& e) noexcept
        : ts(e.ts),
          id(e.id),
          price(e.price),
          qty(e.qty),
          filled(e.filled),
          side(static_cast<Side>(e.side)),
          action(static_cast<Action>(e.action)) {}

    constexpr nq_event Raw() const noexcept {
        return nq_event{ts, id, price, qty, filled,
                        static_cast<std::uint8_t>(side), static_cast<std::uint8_t>(action)};
    }
};

struct Control {
    ControlType type = ControlType::Reset;
    std::int64_t ts = 0;

    constexpr Control() = default;
    constexpr Control(ControlType t, std::int64_t at) noexcept : type(t), ts(at) {}
    constexpr explicit Control(const nq_control& c) noexcept
        : type(static_cast<ControlType>(c.type)), ts(c.ts) {}

    constexpr nq_control Raw() const noexcept {
        return nq_control{static_cast<std::uint8_t>(type), ts};
    }
};

}  // namespace nq
