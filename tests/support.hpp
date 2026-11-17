#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <nq/abi.h>
#include <nq/instrument.hpp>

#include <Feature/RowBuffer.h>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace nqtest {

inline constexpr std::int64_t kSec = 1'000'000'000;
inline constexpr std::int64_t kMin = 60 * kSec;

inline std::int64_t Scaled(double x, std::int64_t scale) { return std::llround(x * static_cast<double>(scale)); }
inline std::int64_t Sec(double t) { return Scaled(t, kSec); }
inline std::int64_t P(double x) { return Scaled(x, NQ_PRICE_SCALE); }
inline std::int64_t Q(double x) { return Scaled(x, NQ_QTY_SCALE); }
inline bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }
inline bool NearOrBothNan(double a, double b) { return (std::isnan(a) && std::isnan(b)) || Near(a, b); }

template <class Exception, class Step>
bool Throws(Step&& step) {
    try {
        step();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

inline std::optional<nq::Instrument> ResolveTwoInstruments(std::uint32_t id) {
    if (id == 1) return nq::Instrument{1, "AAA", 10, 1};
    if (id == 2) return nq::Instrument{2, "BBB", 5, 100};
    return std::nullopt;
}

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

inline nq_ohlcv Ohlcv(std::int64_t ts, std::uint32_t instrument, double open, double high, double low, double close,
                      double volume) {
    return nq_ohlcv{ts, P(open), P(high), P(low), P(close), volume, instrument};
}

template <std::size_t N>
bool RowIs(const nq::RowBuffer& rows, std::size_t i, std::int64_t ts, const std::array<double, N>& want) {
    if (i >= rows.RowCount() || rows.Ts(i) != ts) return false;
    const auto r = rows.Row(i);
    for (std::size_t c = 0; c < N; ++c) {
        if (!NearOrBothNan(r[c], want[c])) return false;
    }
    return true;
}

struct BookRow {
    double spread, mid, buy_vol = 0, sell_vol = 0, trades = 0;
};

inline bool RowIs(const nq::RowBuffer& rows, std::size_t i, std::int64_t ts, const BookRow& want) {
    return RowIs(rows, i, ts, std::array{want.spread, want.mid, want.buy_vol, want.sell_vol, want.trades});
}

}  // namespace nqtest
