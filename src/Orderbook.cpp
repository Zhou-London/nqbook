#include <Orderbook.h>

#include <new>

namespace nq {

Orderbook::Orderbook(std::size_t expected_orders) : pool_(expected_orders) {
    orders_.reserve(expected_orders);
}

// Feed Handlers
nlib::qty_t Orderbook::Apply(const nlib::mbo& m) {
    switch (m.action) {
        case nlib::action::add: OnOrder(m); return 0;
        case nlib::action::cancel: OnCancel(m); return 0;
        case nlib::action::modify: OnModify(m); return 0;
        case nlib::action::trade: return OnTrade(m);
        case nlib::action::clear: OnClear(m); return 0;
    }
    return 0;
}

std::size_t Orderbook::Drain(Queue& q, std::size_t max) {
    std::size_t n = 0;
    while (n < max) {
        auto m = q.try_pop();
        if (!m) break;
        Apply(*m);
        ++n;
    }
    return n;
}

void Orderbook::OnOrder(const nlib::mbo& m) {
    ++stats_.adds;
    last_ts_ = m.ts;
    if (m.qty <= 0) { ++stats_.bad_qty; return; }
    if (orders_.contains(m.id)) { ++stats_.duplicate_add; return; }

    nlib::order* o = pool_.Alloc();
    o->id = m.id;
    o->price = m.price;
    o->qty = m.qty;
    o->ts = m.ts;
    o->side = m.side;
    o->prev = nullptr;
    o->next = nullptr;
    o->level = nullptr;

    Link(o, FindOrCreateLevel(m.side, m.price));
    orders_.try_emplace(nlib::order_id{m.id}, o);
}

void Orderbook::OnCancel(const nlib::mbo& m) {
    ++stats_.cancels;
    last_ts_ = m.ts;
    nlib::order* o = Lookup(m.id);
    if (!o) { ++stats_.unknown_cancel; return; }

    if (m.qty <= 0 || m.qty >= o->qty) {
        Remove(o);
        return;
    }
    o->qty -= m.qty;
    o->level->qty -= m.qty;
}

void Orderbook::OnModify(const nlib::mbo& m) {
    ++stats_.modifies;
    last_ts_ = m.ts;
    nlib::order* o = Lookup(m.id);
    if (!o) { ++stats_.unknown_modify; return; }

    if (m.qty <= 0) {
        Remove(o);
        return;
    }
    if (m.price != o->price) {
        Unlink(o);
        o->price = m.price;
        o->qty = m.qty;
        o->ts = m.ts;
        Link(o, FindOrCreateLevel(o->side, m.price));
        return;
    }
    if (m.qty > o->qty && (o->prev || o->next)) {
        nlib::level* lv = o->level;
        Unlink(o);
        o->qty = m.qty;
        o->ts = m.ts;
        Link(o, lv);
        return;
    }
    o->level->qty -= o->qty - m.qty;
    o->qty = m.qty;
}

nlib::qty_t Orderbook::OnTrade(const nlib::mbo& m) {
    ++stats_.trades;
    last_ts_ = m.ts;
    if (m.qty <= 0) { ++stats_.bad_qty; return 0; }

    nlib::qty_t filled = 0;
    if (m.id != 0) {
        nlib::order* o = Lookup(m.id);
        if (o) {
            filled = Fill(o, m.qty);
            if (filled < m.qty) ++stats_.over_trade;
        } else {
            ++stats_.unknown_trade;
        }
    } else {
        filled = FillLevel(m.side, m.price, m.qty);
        if (filled < m.qty) ++stats_.unmatched_trade;
    }
    return filled;
}

void Orderbook::OnClear(const nlib::mbo& m) {
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

const nlib::level* Orderbook::Level(nlib::side s, nlib::price_t p) const noexcept {
    if (s == nlib::side::bid) {
        auto it = bids_.find(p);
        return it == bids_.end() ? nullptr : &it->second;
    }
    auto it = asks_.find(p);
    return it == asks_.end() ? nullptr : &it->second;
}

const nlib::order* Orderbook::Find(nlib::order_id id) const noexcept {
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
nlib::order* Orderbook::Lookup(nlib::order_id id) noexcept {
    auto it = orders_.find(id);
    return it == orders_.end() ? nullptr : it->second;
}

nlib::level* Orderbook::FindOrCreateLevel(nlib::side s, nlib::price_t p) {
    auto emplace = [p](auto& levels) -> nlib::level* {
        auto [it, inserted] = levels.try_emplace(p);
        if (inserted) it->second = nlib::level{p, 0, 0, nullptr, nullptr};
        return &it->second;
    };
    return s == nlib::side::bid ? emplace(bids_) : emplace(asks_);
}

nlib::level* Orderbook::FindLevel(nlib::side s, nlib::price_t p) noexcept {
    if (s == nlib::side::bid) {
        auto it = bids_.find(p);
        return it == bids_.end() ? nullptr : &it->second;
    }
    auto it = asks_.find(p);
    return it == asks_.end() ? nullptr : &it->second;
}

void Orderbook::EraseLevel(nlib::level* lv, nlib::side s) {
    if (s == nlib::side::bid) bids_.erase(lv->price);
    else asks_.erase(lv->price);
}

void Orderbook::Link(nlib::order* o, nlib::level* lv) noexcept {
    o->level = lv;
    o->prev = lv->tail;
    o->next = nullptr;
    if (lv->tail) lv->tail->next = o;
    else lv->head = o;
    lv->tail = o;
    lv->qty += o->qty;
    ++lv->count;
}

void Orderbook::Unlink(nlib::order* o) {
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
    if (lv->count == 0) EraseLevel(lv, o->side);
}

void Orderbook::Remove(nlib::order* o) {
    Unlink(o);
    orders_.erase(o->id);
    pool_.Free(o);
}

nlib::qty_t Orderbook::Fill(nlib::order* o, nlib::qty_t qty) {
    const nlib::qty_t filled = qty < o->qty ? qty : o->qty;
    if (filled == o->qty) {
        Remove(o);
    } else {
        o->qty -= filled;
        o->level->qty -= filled;
    }
    return filled;
}

nlib::qty_t Orderbook::FillLevel(nlib::side s, nlib::price_t p, nlib::qty_t qty) {
    nlib::level* lv = FindLevel(s, p);
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
