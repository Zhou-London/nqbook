#include <Orderbook.h>

#include <cstdio>
#include <thread>

#include "support.hpp"

using nq::Orderbook;
using namespace nqtest;


int main() {
    Orderbook ob(4);

    ob.Apply(Add(1, 0, 1, NQ_BID, 100, 10));
    ob.Apply(Add(1, 0, 2, NQ_BID, 100, 5));
    ob.Apply(Add(1, 0, 3, NQ_BID, 99, 7));
    ob.Apply(Add(1, 0, 4, NQ_ASK, 101, 3));
    ob.Apply(Add(1, 0, 5, NQ_ASK, 102, 4));
    CHECK(ob.OrderCount() == 5);
    CHECK(ob.BestBid()->price == 100 && ob.BestBid()->qty == 15 && ob.BestBid()->count == 2);
    CHECK(ob.BestAsk()->price == 101 && ob.BestAsk()->qty == 3);
    CHECK(ob.BestBid()->head->id == 1 && ob.BestBid()->tail->id == 2);

    ob.Apply(Add(1, 0, 1, NQ_BID, 100, 10));
    CHECK(ob.GetStats().add_duplicate == 1 && ob.OrderCount() == 5);

    ob.Apply(Cancel(2, 0, 1, 4));
    CHECK(ob.FindOrder(1)->qty == 6 && ob.BestBid()->qty == 11);
    ob.Apply(Cancel(2, 0, 1));
    CHECK(ob.FindOrder(1) == nullptr && ob.BestBid()->qty == 5 && ob.BestBid()->head->id == 2);
    ob.Apply(Cancel(2, 0, 1));
    CHECK(ob.GetStats().cancel_unknown == 1);

    ob.Apply(Modify(3, 0, 2, 100, 3));
    CHECK(ob.FindOrder(2)->qty == 3 && ob.BestBid()->qty == 3);
    ob.Apply(Add(1, 0, 6, NQ_BID, 100, 1));
    CHECK(ob.BestBid()->head->id == 2);
    ob.Apply(Modify(3, 0, 2, 100, 9));
    CHECK(ob.BestBid()->head->id == 6 && ob.BestBid()->tail->id == 2 && ob.BestBid()->qty == 10);
    ob.Apply(Modify(3, 0, 2, 98, 9));
    CHECK(ob.BestBid()->price == 100 && ob.BestBid()->qty == 1);
    CHECK(ob.FindLevel(NQ_BID, 98)->qty == 9 && ob.BidLevelCount() == 3);
    ob.Apply(Modify(3, 0, 6, 100, 0));
    CHECK(ob.BestBid()->price == 99 && ob.BidLevelCount() == 2);
    ob.Apply(Modify(3, 0, 3, 99, 20));
    CHECK(ob.FindOrder(3)->qty == 20 && ob.BestBid()->qty == 20 && ob.BestBid()->count == 1);

    CHECK(ob.Apply(Trade(4, 0, 4, NQ_ASK, 101, 2)) == 2);
    CHECK(ob.FindOrder(4)->qty == 1 && ob.BestAsk()->qty == 1);
    CHECK(ob.Apply(Trade(4, 0, 4, NQ_ASK, 101, 5)) == 1);
    CHECK(ob.FindOrder(4) == nullptr && ob.BestAsk()->price == 102 && ob.GetStats().trade_over_order == 1);
    CHECK(ob.Apply(Trade(4, 0, 99, NQ_ASK, 102, 1)) == 0 && ob.GetStats().trade_unknown == 1);

    ob.Apply(Add(1, 0, 7, NQ_BID, 99, 2));
    CHECK(ob.Apply(Trade(4, 0, 0, NQ_BID, 99, 21)) == 21);
    CHECK(ob.FindOrder(3) == nullptr && ob.FindOrder(7)->qty == 1 && ob.BestBid()->price == 99 && ob.BestBid()->qty == 1);
    CHECK(ob.Apply(Trade(4, 0, 0, NQ_BID, 99, 5)) == 1);
    CHECK(ob.FindLevel(NQ_BID, 99) == nullptr && ob.BestBid()->price == 98);
    CHECK(ob.GetStats().trade_over_level == 1);
    CHECK(ob.Apply(Trade(4, 0, 0, NQ_BID, 50, 5)) == 0);
    CHECK(ob.GetStats().trade_over_level == 2);

    ob.Apply(Clear(9, 0));
    CHECK(ob.OrderCount() == 0 && ob.BestBid() == nullptr && ob.BestAsk() == nullptr);

    for (std::uint64_t i = 1; i <= 1000; ++i) ob.Apply(Add(1, 0, i, NQ_ASK, 200 + (i % 50), 1));
    CHECK(ob.OrderCount() == 1000 && ob.AskLevelCount() == 50);
    for (std::uint64_t i = 1; i <= 1000; i += 2) ob.Apply(Cancel(2, 0, i));
    CHECK(ob.OrderCount() == 500);
    for (std::uint64_t i = 2; i <= 1000; i += 2) ob.Apply(Cancel(2, 0, i));
    CHECK(ob.OrderCount() == 0 && ob.AskLevelCount() == 0);

    Orderbook::Queue q(16);
    Orderbook ob2;
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= 10000; ++i)
            while (!q.try_push(Add(1, 0, i, i % 2 ? NQ_BID : NQ_ASK, 1000 + (i % 7), 1))) {}
    });
    std::size_t drained = 0;
    while (drained < 10000) drained += ob2.Consume(q);
    producer.join();
    CHECK(ob2.OrderCount() == 10000 && ob2.LastTs() == 1);

    std::printf("ok sizeof(nq_mbo)=%zu sizeof(order)=%zu sizeof(level)=%zu\n",
                sizeof(nq_mbo), sizeof(nlib::order), sizeof(nlib::level));
    return 0;
}
