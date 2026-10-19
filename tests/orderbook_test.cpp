#include <Orderbook.h>

#include <cassert>
#include <cstdio>
#include <thread>

using nlib::action;
using nlib::mbo;
using nlib::side;
using nq::Orderbook;

static mbo Add(std::uint64_t id, side s, std::int64_t p, std::int64_t q, std::int64_t ts = 1) {
    return mbo{ts, id, p, q, s, action::add};
}
static mbo Cancel(std::uint64_t id, std::int64_t q = 0) { return mbo{2, id, 0, q, side::bid, action::cancel}; }
static mbo Modify(std::uint64_t id, std::int64_t p, std::int64_t q) { return mbo{3, id, p, q, side::bid, action::modify}; }
static mbo Trade(std::uint64_t id, side s, std::int64_t p, std::int64_t q) { return mbo{4, id, p, q, s, action::trade}; }

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    Orderbook ob(4);

    ob.Apply(Add(1, side::bid, 100, 10));
    ob.Apply(Add(2, side::bid, 100, 5));
    ob.Apply(Add(3, side::bid, 99, 7));
    ob.Apply(Add(4, side::ask, 101, 3));
    ob.Apply(Add(5, side::ask, 102, 4));
    CHECK(ob.OrderCount() == 5);
    CHECK(ob.BestBid()->price == 100 && ob.BestBid()->qty == 15 && ob.BestBid()->count == 2);
    CHECK(ob.BestAsk()->price == 101 && ob.BestAsk()->qty == 3);
    CHECK(ob.BestBid()->head->id == 1 && ob.BestBid()->tail->id == 2);

    ob.Apply(Add(1, side::bid, 100, 10));
    CHECK(ob.GetStats().add_duplicate == 1 && ob.OrderCount() == 5);

    ob.Apply(Cancel(1, 4));
    CHECK(ob.FindOrder(1)->qty == 6 && ob.BestBid()->qty == 11);
    ob.Apply(Cancel(1));
    CHECK(ob.FindOrder(1) == nullptr && ob.BestBid()->qty == 5 && ob.BestBid()->head->id == 2);
    ob.Apply(Cancel(1));
    CHECK(ob.GetStats().cancel_unknown == 1);

    ob.Apply(Modify(2, 100, 3));
    CHECK(ob.FindOrder(2)->qty == 3 && ob.BestBid()->qty == 3);
    ob.Apply(Add(6, side::bid, 100, 1));
    CHECK(ob.BestBid()->head->id == 2);
    ob.Apply(Modify(2, 100, 9));
    CHECK(ob.BestBid()->head->id == 6 && ob.BestBid()->tail->id == 2 && ob.BestBid()->qty == 10);
    ob.Apply(Modify(2, 98, 9));
    CHECK(ob.BestBid()->price == 100 && ob.BestBid()->qty == 1);
    CHECK(ob.FindLevel(side::bid, 98)->qty == 9 && ob.BidLevelCount() == 3);
    ob.Apply(Modify(6, 100, 0));
    CHECK(ob.BestBid()->price == 99 && ob.BidLevelCount() == 2);
    ob.Apply(Modify(3, 99, 20));
    CHECK(ob.FindOrder(3)->qty == 20 && ob.BestBid()->qty == 20 && ob.BestBid()->count == 1);

    CHECK(ob.Apply(Trade(4, side::ask, 101, 2)) == 2);
    CHECK(ob.FindOrder(4)->qty == 1 && ob.BestAsk()->qty == 1);
    CHECK(ob.Apply(Trade(4, side::ask, 101, 5)) == 1);
    CHECK(ob.FindOrder(4) == nullptr && ob.BestAsk()->price == 102 && ob.GetStats().trade_over_order == 1);
    CHECK(ob.Apply(Trade(99, side::ask, 102, 1)) == 0 && ob.GetStats().trade_unknown == 1);

    ob.Apply(Add(7, side::bid, 99, 2));
    CHECK(ob.Apply(Trade(0, side::bid, 99, 21)) == 21);
    CHECK(ob.FindOrder(3) == nullptr && ob.FindOrder(7)->qty == 1 && ob.BestBid()->price == 99 && ob.BestBid()->qty == 1);
    CHECK(ob.Apply(Trade(0, side::bid, 99, 5)) == 1);
    CHECK(ob.FindLevel(side::bid, 99) == nullptr && ob.BestBid()->price == 98);
    CHECK(ob.GetStats().trade_over_level == 1);
    CHECK(ob.Apply(Trade(0, side::bid, 50, 5)) == 0);
    CHECK(ob.GetStats().trade_over_level == 2);

    ob.Apply(mbo{9, 0, 0, 0, side::bid, action::clear});
    CHECK(ob.OrderCount() == 0 && ob.BestBid() == nullptr && ob.BestAsk() == nullptr);

    for (std::uint64_t i = 1; i <= 1000; ++i) ob.Apply(Add(i, side::ask, 200 + (i % 50), 1));
    CHECK(ob.OrderCount() == 1000 && ob.AskLevelCount() == 50);
    for (std::uint64_t i = 1; i <= 1000; i += 2) ob.Apply(Cancel(i));
    CHECK(ob.OrderCount() == 500);
    for (std::uint64_t i = 2; i <= 1000; i += 2) ob.Apply(Cancel(i));
    CHECK(ob.OrderCount() == 0 && ob.AskLevelCount() == 0);

    Orderbook::Queue q(16);
    Orderbook ob2;
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= 10000; ++i)
            while (!q.try_push(Add(i, i % 2 ? side::bid : side::ask, 1000 + (i % 7), 1))) {}
    });
    std::size_t drained = 0;
    while (drained < 10000) drained += ob2.Consume(q);
    producer.join();
    CHECK(ob2.OrderCount() == 10000 && ob2.LastTs() == 1);

    std::printf("ok sizeof(mbo)=%zu sizeof(order)=%zu sizeof(level)=%zu\n",
                sizeof(mbo), sizeof(nlib::order), sizeof(nlib::level));
    return 0;
}
