#include <Pipeline.h>

#include <chrono>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include <Orderbook.h>

namespace nq {
namespace {

// Pushes `r` into `out`, waiting while it is full; between stages a record is
// delayed rather than dropped.
void Forward(RecordQueue& out, Record&& r) {
  while (!out.try_push(std::move(r)))
    std::this_thread::sleep_for(std::chrono::microseconds(50));
}

// Routes one feed event to the handler its type and action select.
void Apply(Orderbook& book, const FeedEvent& e) {
  std::visit(overloaded{[&](const nlib::order& o) {
                          o.action == nlib::order_action::cancel ? book.OnCancel(o)
                                                                 : book.OnOrder(o);
                        },
                        [&](const nlib::trade& t) { book.OnTrade(t); }},
             e);
}

Record ToRecord(FeedEvent&& e) {
  return std::visit([](auto& rec) { return Record(rec); }, e);
}

}  // namespace

void RunBook(FeedQueue& in, RecordQueue& out, std::stop_token stop) {
  Orderbook book;
  auto next_snapshot = std::chrono::steady_clock::now() + kSnapshotPeriod;
  while (!stop.stop_requested()) {
    if (std::optional<FeedEvent> e = in.try_pop()) {
      Apply(book, *e);
      Forward(out, ToRecord(std::move(*e)));
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Snapshots fire on the wall clock, book activity or not; += keeps the
    // cadence drift-free.
    if (std::chrono::steady_clock::now() >= next_snapshot) {
      Forward(out, book.OnSnapshot());
      next_snapshot += kSnapshotPeriod;
    }
  }
  // The feed thread stops first, so `in` only shrinks here.
  while (std::optional<FeedEvent> e = in.try_pop()) {
    Apply(book, *e);
    Forward(out, ToRecord(std::move(*e)));
  }
}

}  // namespace nq
