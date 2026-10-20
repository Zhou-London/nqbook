#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <nlib/common.h>
#include <nq/abi.h>

#include <Defaults.h>
#include <nlib/map.h>
#include <nlib/memory_pool.h>
#include <nlib/single_queue.h>

namespace nq {

class Orderbook {
public:
    using Queue = nlib::single_queue<nq_mbo>;
    using BidLevels = std::map<nlib::price_t, nlib::level, std::greater<nlib::price_t>>;
    using AskLevels = std::map<nlib::price_t, nlib::level, std::less<nlib::price_t>>;

    struct Stats {
        std::uint64_t adds = 0;
        std::uint64_t cancels = 0;
        std::uint64_t modifies = 0;
        std::uint64_t trades = 0;
        std::uint64_t clears = 0;
        std::uint64_t add_duplicate = 0;
        std::uint64_t cancel_unknown = 0;
        std::uint64_t modify_unknown = 0;
        std::uint64_t trade_unknown = 0;
        std::uint64_t trade_over_level = 0;
        std::uint64_t trade_over_order = 0;
        std::uint64_t qty_invalid = 0;
    };

    explicit Orderbook(std::size_t expected_orders = defaults::kExpectedOrders);
    ~Orderbook() = default;

    Orderbook(const Orderbook&) = delete;
    Orderbook& operator=(const Orderbook&) = delete;

    // Feed Handlers
    nlib::qty_t Apply(const nq_mbo& m);
    std::size_t Consume(Queue& q, std::size_t max = std::numeric_limits<std::size_t>::max());

    void OnOrder(const nq_mbo& m);
    void OnCancel(const nq_mbo& m);
    void OnModify(const nq_mbo& m);
    nlib::qty_t OnTrade(const nq_mbo& m);
    void OnClear(const nq_mbo& m);

    // Queries
    const nlib::level* BestBid() const noexcept;
    const nlib::level* BestAsk() const noexcept;
    const BidLevels& Bids() const noexcept { return bids_; }
    const AskLevels& Asks() const noexcept { return asks_; }
    const nlib::level* FindLevel(std::uint8_t s, nlib::price_t p) const noexcept;
    const nlib::order* FindOrder(nlib::order_id id) const noexcept;

    std::size_t OrderCount() const noexcept { return orders_.size(); }
    std::size_t BidLevelCount() const noexcept { return bids_.size(); }
    std::size_t AskLevelCount() const noexcept { return asks_.size(); }

    nlib::ns_t LastTs() const noexcept { return last_ts_; }
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

    nlib::order* MutableOrder(nlib::order_id id) noexcept;
    nlib::level* FindOrCreateLevel(std::uint8_t s, nlib::price_t p);
    nlib::level* MutableLevel(std::uint8_t s, nlib::price_t p) noexcept;
    void RemoveLevel(nlib::level* lv, std::uint8_t s);

    static void LinkOrder(nlib::order* o, nlib::level* lv) noexcept;
    void UnlinkOrder(nlib::order* o);
    void RemoveOrder(nlib::order* o);
    nlib::qty_t Fill(nlib::order* o, nlib::qty_t qty);
    nlib::qty_t FillLevel(std::uint8_t s, nlib::price_t p, nlib::qty_t qty);

    BidLevels bids_;
    AskLevels asks_;
    nlib::map<nlib::order_id, nlib::order*> orders_;
    OrderPool pool_;
    Stats stats_;
    nlib::ns_t last_ts_ = 0;
};

}  // namespace nq
