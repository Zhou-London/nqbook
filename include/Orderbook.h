#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <nlib/common.h>
#include <nlib/map.h>
#include <nlib/memory_pool.h>
#include <nlib/single_queue.h>

class Orderbook {
public:
    using Queue = nlib::single_queue<nlib::mbo>;
    using BidLevels = std::map<nlib::price_t, nlib::level, std::greater<nlib::price_t>>;
    using AskLevels = std::map<nlib::price_t, nlib::level, std::less<nlib::price_t>>;

    struct Stats {
        std::uint64_t adds = 0;
        std::uint64_t cancels = 0;
        std::uint64_t modifies = 0;
        std::uint64_t trades = 0;
        std::uint64_t clears = 0;
        std::uint64_t duplicate_add = 0;
        std::uint64_t unknown_cancel = 0;
        std::uint64_t unknown_modify = 0;
        std::uint64_t unknown_trade = 0;
        std::uint64_t unmatched_trade = 0;
        std::uint64_t over_trade = 0;
        std::uint64_t bad_qty = 0;
    };

    explicit Orderbook(std::size_t expected_orders = 1 << 16);
    ~Orderbook() = default;

    Orderbook(const Orderbook&) = delete;
    Orderbook& operator=(const Orderbook&) = delete;

    // Feed Handlers
    void Apply(const nlib::mbo& m);
    std::size_t Drain(Queue& q, std::size_t max = std::numeric_limits<std::size_t>::max());

    void OnOrder(const nlib::mbo& m);
    void OnCancel(const nlib::mbo& m);
    void OnModify(const nlib::mbo& m);
    void OnTrade(const nlib::mbo& m);
    void OnClear(const nlib::mbo& m);

    // Queries
    const nlib::level* BestBid() const noexcept;
    const nlib::level* BestAsk() const noexcept;
    const BidLevels& Bids() const noexcept { return bids_; }
    const AskLevels& Asks() const noexcept { return asks_; }
    const nlib::level* Level(nlib::side s, nlib::price_t p) const noexcept;
    const nlib::order* Find(nlib::order_id id) const noexcept;

    std::size_t OrderCount() const noexcept { return orders_.size(); }
    std::size_t BidLevelCount() const noexcept { return bids_.size(); }
    std::size_t AskLevelCount() const noexcept { return asks_.size(); }

    nlib::ns_t LastTs() const noexcept { return last_ts_; }
    std::uint64_t TradeCount() const noexcept { return trade_count_; }
    const nlib::trade& LastTrade() const noexcept { return last_trade_; }
    const Stats& GetStats() const noexcept { return stats_; }

private:
    class OrderPool {
    public:
        explicit OrderPool(std::size_t chunk);

        nlib::order* Alloc();
        void Free(nlib::order* o) noexcept;
        void Reset() noexcept;

    private:
        std::size_t chunk_;
        std::vector<std::unique_ptr<nlib::memory_pool>> chunks_;
        nlib::order* free_ = nullptr;
    };

    nlib::order* Lookup(nlib::order_id id) noexcept;
    nlib::level* FindOrCreateLevel(nlib::side s, nlib::price_t p);
    nlib::level* FindLevel(nlib::side s, nlib::price_t p) noexcept;
    void EraseLevel(nlib::level* lv, nlib::side s);

    static void Link(nlib::order* o, nlib::level* lv) noexcept;
    void Unlink(nlib::order* o);
    void Remove(nlib::order* o);
    nlib::qty_t Fill(nlib::order* o, nlib::qty_t qty);
    nlib::qty_t FillLevel(nlib::side s, nlib::price_t p, nlib::qty_t qty);

    BidLevels bids_;
    AskLevels asks_;
    nlib::map<nlib::order_id, nlib::order*> orders_;
    OrderPool pool_;
    Stats stats_;
    nlib::trade last_trade_{};
    std::uint64_t trade_count_ = 0;
    nlib::ns_t last_ts_ = 0;
};
