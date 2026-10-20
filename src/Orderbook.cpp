#include <Orderbook.h>

#include <new>

namespace nq {

Orderbook::Orderbook(std::size_t expected_orders) : pool_(expected_orders) {
    orders_.reserve(expected_orders);
}

// Feed Handlers
nlib::qty_t Orderbook::Apply(const nq_mbo& m) {
    switch (m.action) {
        case NQ_ADD: OnOrder(m); return 0;
        case NQ_CANCEL: OnCancel(m); return 0;
        case NQ_MODIFY: OnModify(m); return 0;
        case NQ_TRADE: return OnTrade(m);
        case NQ_CLEAR: OnClear(m); return 0;
    }
    return 0;
}

std::size_t Orderbook::Consume(Queue& q, std::size_t max) {
    std::size_t n = 0;
    while (n < max) {
        auto m = q.try_pop();
        if (!m) break;
        Apply(*m);
        ++n;
    }
    return n;
}

void Orderbook::OnOrder(const nq_mbo& m) {
    ++stats_.adds;
    last_ts_ = m.ts;
    if (m.qty <= 0) { ++stats_.qty_invalid; return; }
    if (orders_.contains(m.id)) { ++stats_.add_duplicate; return; }

    nlib::order* o = pool_.Alloc();
    o->id = m.id;
    o->price = m.price;
    o->qty = m.qty;
    o->ts = m.ts;
    o->side = m.side;
    o->prev = nullptr;
    o->next = nullptr;
    o->level = nullptr;

    LinkOrder(o, FindOrCreateLevel(m.side, m.price));
    orders_.try_emplace(nlib::order_id{m.id}, o);
}

void Orderbook::OnCancel(const nq_mbo& m) {
    ++stats_.cancels;
    last_ts_ = m.ts;
    nlib::order* o = MutableOrder(m.id);
    if (!o) { ++stats_.cancel_unknown; return; }

    if (m.qty <= 0 || m.qty >= o->qty) {
        RemoveOrder(o);
        return;
    }
    o->qty -= m.qty;
    o->level->qty -= m.qty;
}

void Orderbook::OnModify(const nq_mbo& m) {
    ++stats_.modifies;
    last_ts_ = m.ts;
    nlib::order* o = MutableOrder(m.id);
    if (!o) { ++stats_.modify_unknown; return; }

    if (m.qty <= 0) {
        RemoveOrder(o);
        return;
    }
    if (m.price != o->price) {
        UnlinkOrder(o);
        o->price = m.price;
        o->qty = m.qty;
        o->ts = m.ts;
        LinkOrder(o, FindOrCreateLevel(o->side, m.price));
        return;
    }
    if (m.qty > o->qty && (o->prev || o->next)) {
        nlib::level* lv = o->level;
        UnlinkOrder(o);
        o->qty = m.qty;
        o->ts = m.ts;
        LinkOrder(o, lv);
        return;
    }
    o->level->qty -= o->qty - m.qty;
    o->qty = m.qty;
}

nlib::qty_t Orderbook::OnTrade(const nq_mbo& m) {
    ++stats_.trades;
    last_ts_ = m.ts;
    if (m.qty <= 0) { ++stats_.qty_invalid; return 0; }

    nlib::qty_t filled = 0;
    if (m.id != 0) {
        nlib::order* o = MutableOrder(m.id);
        if (o) {
            filled = Fill(o, m.qty);
            if (filled < m.qty) ++stats_.trade_over_order;
        } else {
            ++stats_.trade_unknown;
        }
    } else {
        filled = FillLevel(m.side, m.price, m.qty);
        if (filled < m.qty) ++stats_.trade_over_level;
    }
    return filled;
}

void Orderbook::OnClear(const nq_mbo& m) {
    ++stats_.clears;
    last_ts_ = m.ts;
    bids_.clear();
    asks_.clear();
    orders_.clear();
    pool_.Reset();
}

// Queries
const nlib::level* Orderbook::BestBid() const noexcept {
    return bids_.empty() ? nullptr : &bids_.begin()->second;
}

const nlib::level* Orderbook::BestAsk() const noexcept {
    return asks_.empty() ? nullptr : &asks_.begin()->second;
}

const nlib::level* Orderbook::FindLevel(std::uint8_t s, nlib::price_t p) const noexcept {
    if (s == NQ_BID) {
        auto it = bids_.find(p);
        return it == bids_.end() ? nullptr : &it->second;
    }
    auto it = asks_.find(p);
    return it == asks_.end() ? nullptr : &it->second;
}

const nlib::order* Orderbook::FindOrder(nlib::order_id id) const noexcept {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : it->second;
}

// OrderPool
Orderbook::OrderPool::OrderPool(std::size_t chunk) : chunk_(chunk > 0 ? chunk : 1) {}

nlib::order* Orderbook::OrderPool::Alloc() {
    if (free_) {
        nlib::order* o = free_;
        free_ = o->next;
        return o;
    }
    void* p = chunks_.empty() ? nullptr : chunks_.back()->allocate();
    if (!p) {
        chunks_.push_back(std::make_unique<nlib::memory_pool>(
            sizeof(nlib::order), chunk_, alignof(nlib::order)));
        chunk_ *= 2;
        p = chunks_.back()->allocate();
    }
    return ::new (p) nlib::order{};
}

void Orderbook::OrderPool::Free(nlib::order* o) noexcept {
    o->next = free_;
    free_ = o;
}

void Orderbook::OrderPool::Reset() noexcept {
    free_ = nullptr;
    chunks_.clear();
}

// Internals
nlib::order* Orderbook::MutableOrder(nlib::order_id id) noexcept {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : it->second;
}

nlib::level* Orderbook::FindOrCreateLevel(std::uint8_t s, nlib::price_t p) {
    auto emplace = [p](auto& levels) -> nlib::level* {
        auto [it, inserted] = levels.try_emplace(p);
        if (inserted) it->second = nlib::level{p, 0, 0, nullptr, nullptr};
        return &it->second;
    };
    return s == NQ_BID ? emplace(bids_) : emplace(asks_);
}

nlib::level* Orderbook::MutableLevel(std::uint8_t s, nlib::price_t p) noexcept {
    if (s == NQ_BID) {
        auto it = bids_.find(p);
        return it == bids_.end() ? nullptr : &it->second;
    }
    auto it = asks_.find(p);
    return it == asks_.end() ? nullptr : &it->second;
}

void Orderbook::RemoveLevel(nlib::level* lv, std::uint8_t s) {
    if (s == NQ_BID) bids_.erase(lv->price);
    else asks_.erase(lv->price);
}

void Orderbook::LinkOrder(nlib::order* o, nlib::level* lv) noexcept {
    o->level = lv;
    o->prev = lv->tail;
    o->next = nullptr;
    if (lv->tail) lv->tail->next = o;
    else lv->head = o;
    lv->tail = o;
    lv->qty += o->qty;
    ++lv->count;
}

void Orderbook::UnlinkOrder(nlib::order* o) {
    nlib::level* lv = o->level;
    if (o->prev) o->prev->next = o->next;
    else lv->head = o->next;
    if (o->next) o->next->prev = o->prev;
    else lv->tail = o->prev;
    lv->qty -= o->qty;
    --lv->count;
    o->prev = nullptr;
    o->next = nullptr;
    o->level = nullptr;
    if (lv->count == 0) RemoveLevel(lv, o->side);
}

void Orderbook::RemoveOrder(nlib::order* o) {
    UnlinkOrder(o);
    orders_.erase(o->id);
    pool_.Free(o);
}

nlib::qty_t Orderbook::Fill(nlib::order* o, nlib::qty_t qty) {
    const nlib::qty_t filled = qty < o->qty ? qty : o->qty;
    if (filled == o->qty) {
        RemoveOrder(o);
    } else {
        o->qty -= filled;
        o->level->qty -= filled;
    }
    return filled;
}

nlib::qty_t Orderbook::FillLevel(std::uint8_t s, nlib::price_t p, nlib::qty_t qty) {
    nlib::level* lv = MutableLevel(s, p);
    if (!lv) return 0;

    nlib::qty_t remaining = qty;
    while (remaining > 0 && lv->head) {
        nlib::order* o = lv->head;
        const bool last = o->next == nullptr;
        remaining -= Fill(o, remaining);
        if (last) break;
    }
    return qty - remaining;
}

}  // namespace nq
