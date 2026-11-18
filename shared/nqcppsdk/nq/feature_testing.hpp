#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <nq/feature.hpp>

namespace nq::testing {

class MockBook {
public:
    MockBook() = default;
    MockBook(const MockBook&) = delete;
    MockBook& operator=(const MockBook&) = delete;

    MockBook& SetBids(std::vector<Level> levels) { bids_ = std::move(levels); return *this; }
    MockBook& SetAsks(std::vector<Level> levels) { asks_ = std::move(levels); return *this; }
    MockBook& SetTs(std::int64_t ts) { ts_ = ts; return *this; }

    MockBook& Clear() {
        bids_.clear();
        asks_.clear();
        return *this;
    }

    const nq_book_view* Raw() const noexcept { return &view_; }
    BookView View() const noexcept { return BookView(&view_); }

private:
    static const MockBook& Self(const void* ctx) noexcept { return *static_cast<const MockBook*>(ctx); }

    static int Best(const void* ctx, std::uint8_t side, Level* out) noexcept {
        return GetLevel(ctx, side, 0, out);
    }

    static int GetLevel(const void* ctx, std::uint8_t side, std::uint32_t k, Level* out) noexcept {
        const auto& levels = side == NQ_BID ? Self(ctx).bids_ : Self(ctx).asks_;
        if (k >= levels.size()) return 1;
        *out = levels[k];
        return 0;
    }

    static std::int64_t Ts(const void* ctx) noexcept { return Self(ctx).ts_; }

    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::int64_t ts_ = 0;
    nq_book_view view_{this, &MockBook::Best, &MockBook::GetLevel, &MockBook::Ts};
};

inline nq_event MakeEvent(Action action, std::int64_t ts, std::uint64_t id, Side side,
                          std::int64_t price, std::int64_t qty, std::int64_t filled = 0) noexcept {
    return nq_event{ts, id, price, qty, filled,
                    static_cast<std::uint8_t>(side), static_cast<std::uint8_t>(action)};
}

inline nq_event Add(std::int64_t ts, std::uint64_t id, Side side, std::int64_t price,
                    std::int64_t qty) noexcept {
    return MakeEvent(Action::Add, ts, id, side, price, qty);
}

inline nq_event Cancel(std::int64_t ts, std::uint64_t id, Side side, std::int64_t price,
                       std::int64_t qty) noexcept {
    return MakeEvent(Action::Cancel, ts, id, side, price, qty);
}

inline nq_event Modify(std::int64_t ts, std::uint64_t id, Side side, std::int64_t price,
                       std::int64_t qty) noexcept {
    return MakeEvent(Action::Modify, ts, id, side, price, qty);
}

inline nq_event Trade(std::int64_t ts, std::uint64_t maker_id, Side maker_side,
                      std::int64_t price, std::int64_t qty, std::int64_t filled) noexcept {
    return MakeEvent(Action::Trade, ts, maker_id, maker_side, price, qty, filled);
}

inline nq_event Clear(std::int64_t ts) noexcept {
    return MakeEvent(Action::Clear, ts, 0, Side::Bid, 0, 0);
}

inline nq_ohlcv MakeOhlcv(std::int64_t ts, std::int64_t open, std::int64_t high, std::int64_t low,
                          std::int64_t close, double volume) noexcept {
    return nq_ohlcv{ts, open, high, low, close, volume, 0};
}

template <class... Fs>
class Sandbox {
public:
    using BundleType = Bundle<Fs...>;

    explicit Sandbox(const nq_params* params) : bundle_(Params(params)) {}

    MockBook& Book() noexcept { return book_; }
    BundleType& Features() noexcept { return bundle_; }
    const BundleType& Features() const noexcept { return bundle_; }

    void Feed(const nq_event& e) noexcept { bundle_.OnEvent(Event(e), book_.View()); }

    void Feed(std::span<const nq_event> events) noexcept {
        for (const auto& e : events) Feed(e);
    }

    void Feed(const nq_ohlcv& ohlcv) noexcept { bundle_.OnOhlcv(ohlcv); }

    void Feed(std::span<const nq_ohlcv> ohlcvs) noexcept {
        for (const auto& ohlcv : ohlcvs) Feed(ohlcv);
    }

    void Warmup(const nq_ohlcv& ohlcv) noexcept { bundle_.OnWarmup(ohlcv); }

    void Warmup(std::span<const nq_ohlcv> ohlcvs) noexcept {
        for (const auto& ohlcv : ohlcvs) Warmup(ohlcv);
    }

    void Control(ControlType type, std::int64_t ts) noexcept {
        bundle_.OnControl(nq::Control(type, ts));
    }

    std::vector<double> Read() const {
        std::vector<double> out(BundleType::kNcol);
        if constexpr (BundleType::kFeed == nq::Feed::MBO) bundle_.Read(out, book_.View());
        else bundle_.Read(out);
        return out;
    }

private:
    MockBook book_;
    BundleType bundle_;
};

}  // namespace nq::testing
