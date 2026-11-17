#include <Orderbook.h>

namespace nq {

Orderbook::Orderbook(std::size_t expected_orders) : pool_(expected_orders) {
    orders_.reserve(expected_orders);
}

nlib::qty_t Orderbook::Apply(const nq_mbo& m) {
    last_ts_ = m.ts;
    nlib::qty_t filled = 0;
    switch (m.action) {
        case NQ_ADD: OnOrder(m); break;
        case NQ_CANCEL: OnCancel(m); break;
        case NQ_MODIFY: OnModify(m); break;
        case NQ_TRADE: filled = OnTrade(m); break;
        case NQ_CLEAR: OnClear(); break;
    }
    return filled;
}

void Orderbook::OnOrder(const nq_mbo& m) {
    ++stats_.adds;
    if (m.qty <= 0) { ++stats_.qty_invalid; return; }
    auto [slot, inserted] = orders_.try_emplace(nlib::order_id{m.id});
    if (!inserted) { ++stats_.add_duplicate; return; }

    nlib::order* o = pool_.Alloc();
    o->id = m.id;
    o->price = m.price;
    o->qty = m.qty;
    o->ts = m.ts;
    o->side = m.side;
    slot->second = o;

    LinkOrder(o, FindOrCreateLevel(m.side, m.price));
}

void Orderbook::OnCancel(const nq_mbo& m) {
    ++stats_.cancels;
    nlib::order* o = OrderAt(m.id);
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
    nlib::order* o = OrderAt(m.id);
    if (!o) { ++stats_.modify_unknown; return; }

    if (m.qty <= 0) {
        RemoveOrder(o);
        return;
    }
    const bool moves_price = m.price != o->price;
    const bool loses_priority = moves_price || m.qty > o->qty;
    if (!loses_priority) {
        o->level->qty -= o->qty - m.qty;
        o->qty = m.qty;
        return;
    }
    nlib::level* from = o->level;
    UnlinkOrder(o);
    o->price = m.price;
    o->qty = m.qty;
    o->ts = m.ts;
    LinkOrder(o, moves_price ? FindOrCreateLevel(o->side, m.price) : from);
    if (from->count == 0) RemoveLevel(o->side, from);
}

nlib::qty_t Orderbook::OnTrade(const nq_mbo& m) {
    ++stats_.trades;
    if (m.qty <= 0) { ++stats_.qty_invalid; return 0; }

    nlib::qty_t filled = 0;
    if (m.id != 0) {
        nlib::order* o = OrderAt(m.id);
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

void Orderbook::OnClear() {
    ++stats_.clears;
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

const nlib::level* Orderbook::FindLevel(std::uint8_t side, nlib::price_t p) const noexcept {
    return LevelAt(*this, side, p);
}

const nlib::order* Orderbook::FindOrder(nlib::order_id id) const noexcept {
    return OrderAt(id);
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
    return static_cast<nlib::order*>(p);
}

void Orderbook::OrderPool::Free(nlib::order* o) noexcept {
    o->next = free_;
    free_ = o;
}

void Orderbook::OrderPool::Reset() noexcept {
    if (!chunks_.empty()) chunk_ = chunks_.back()->capacity();
    free_ = nullptr;
    chunks_.clear();
}

// Internals
nlib::order* Orderbook::OrderAt(nlib::order_id id) const noexcept {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : it->second;
}

nlib::level* Orderbook::FindOrCreateLevel(std::uint8_t side, nlib::price_t p) {
    return OnSide(*this, side, [p](auto& levels) -> nlib::level* {
        auto [it, inserted] = levels.try_emplace(p);
        if (inserted) it->second = nlib::level{p, 0, 0, nullptr, nullptr};
        return &it->second;
    });
}

void Orderbook::RemoveLevel(std::uint8_t side, nlib::level* lv) {
    OnSide(*this, side, [lv](auto& levels) { levels.erase(lv->price); });
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

void Orderbook::UnlinkOrder(nlib::order* o) noexcept {
    nlib::level* lv = o->level;
    if (o->prev) o->prev->next = o->next;
    else lv->head = o->next;
    if (o->next) o->next->prev = o->prev;
    else lv->tail = o->prev;
    lv->qty -= o->qty;
    --lv->count;
}

void Orderbook::RemoveOrder(nlib::order* o) {
    nlib::level* lv = o->level;
    UnlinkOrder(o);
    if (lv->count == 0) RemoveLevel(o->side, lv);
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

nlib::qty_t Orderbook::FillLevel(std::uint8_t side, nlib::price_t p, nlib::qty_t qty) {
    nlib::level* lv = LevelAt(*this, side, p);
    if (!lv) return 0;

    nlib::qty_t remaining = qty;
    for (nlib::order* o = lv->head; o && remaining > 0;) {
        nlib::order* next = o->next;
        remaining -= Fill(o, remaining);
        o = next;
    }
    return qty - remaining;
}

}  // namespace nq
