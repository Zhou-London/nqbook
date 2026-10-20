#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <nq/abi.h>

#include <Feature/RowBuffer.h>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace nqtest {

inline constexpr std::int64_t kSec = 1'000'000'000;

inline std::int64_t Sec(double t) { return static_cast<std::int64_t>(std::llround(t * static_cast<double>(kSec))); }
inline std::int64_t P(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(NQ_PRICE_SCALE))); }
inline std::int64_t Q(double x) { return static_cast<std::int64_t>(std::llround(x * static_cast<double>(NQ_QTY_SCALE))); }
inline bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

inline nq_mbo Mbo(std::uint8_t action, std::int64_t ts, std::uint32_t instrument, std::uint64_t id, std::uint8_t side,
                  std::int64_t price, std::int64_t qty) {
    return nq_mbo{ts, id, price, qty, instrument, side, action};
}

inline nq_mbo Add(std::int64_t ts, std::uint32_t instrument, std::uint64_t id, std::uint8_t side, std::int64_t price,
                  std::int64_t qty) {
    return Mbo(NQ_ADD, ts, instrument, id, side, price, qty);
}

inline nq_mbo Cancel(std::int64_t ts, std::uint32_t instrument, std::uint64_t id, std::int64_t qty = 0) {
    return Mbo(NQ_CANCEL, ts, instrument, id, NQ_BID, 0, qty);
}

inline nq_mbo Modify(std::int64_t ts, std::uint32_t instrument, std::uint64_t id, std::int64_t price, std::int64_t qty) {
    return Mbo(NQ_MODIFY, ts, instrument, id, NQ_BID, price, qty);
}

inline nq_mbo Trade(std::int64_t ts, std::uint32_t instrument, std::uint64_t maker_id, std::uint8_t maker_side,
                    std::int64_t price, std::int64_t qty) {
    return Mbo(NQ_TRADE, ts, instrument, maker_id, maker_side, price, qty);
}

inline nq_mbo Clear(std::int64_t ts, std::uint32_t instrument) {
    return Mbo(NQ_CLEAR, ts, instrument, 0, NQ_BID, 0, 0);
}

inline bool RowIs(const nq::RowBuffer& rows, std::size_t i, std::int64_t ts, double spread, double mid, double buy,
                  double sell, double trades) {
    if (i >= rows.RowCount()) return false;
    if (rows.Ts(i) != ts) return false;
    const auto r = rows.Row(i);
    const bool book_ok = std::isnan(spread) ? (std::isnan(r[0]) && std::isnan(r[1]))
                                            : (Near(r[0], spread) && Near(r[1], mid));
    return book_ok && Near(r[2], buy) && Near(r[3], sell) && Near(r[4], trades);
}

}  // namespace nqtest
