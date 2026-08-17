#include <Pipeline.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include <Orderbook.h>
#include <nlib/map.h>

namespace nq {
namespace {

// Every 2^10th event is timed around its book apply; sampling keeps the
// clock reads off the per-event path.
constexpr std::uint64_t kSampleMask = (1 << 10) - 1;

// Pushes `r` into `out`, waiting while it is full; between stages a record is
// delayed rather than dropped.
void Forward(RecordQueue& out, Record&& r) {
  while (!out.try_push(std::move(r)))
    std::this_thread::sleep_for(std::chrono::microseconds(50));
}

// Routes one feed event to the handler its type and action select, on the
// instrument's own book.
void Apply(nlib::map<std::uint32_t, Orderbook>& books, const FeedEvent& e) {
  std::visit(overloaded{[&](const nlib::order& o) {
                          Orderbook& book = books[std::uint32_t{o.instrument_id}];
                          switch (o.action) {
                            case nlib::order_action::add:
                              return book.OnOrder(o);
                            case nlib::order_action::cancel:
                              return book.OnCancel(o);
                            case nlib::order_action::modify:
                              return book.OnModify(o);
                            case nlib::order_action::clear:
                              return book.OnClear(o);
                          }
                        },
                        [&](const nlib::trade& t) {
                          books[std::uint32_t{t.instrument_id}].OnTrade(t);
                        }},
             e);
}

Record ToRecord(FeedEvent&& e) {
  return std::visit([](auto& rec) { return Record(rec); }, e);
}

// Refreshes the book gauges; runs at snapshot cadence, where the books are
// walked anyway.
void SetGauges(const nlib::map<std::uint32_t, Orderbook>& books, Metrics& metrics) {
  std::size_t orders = 0;
  std::size_t bytes = 0;
  for (const auto& [id, book] : books) {
    orders += book.size();
    bytes += book.MemoryBytes();
  }
  metrics.book_instruments.Set(books.size());
  metrics.book_resting_orders.Set(orders);
  metrics.book_memory_bytes.Set(bytes);
}

}  // namespace

void RunBook(FeedQueue& in, RecordQueue& out, Metrics& metrics, std::stop_token stop) {
  nlib::map<std::uint32_t, Orderbook> books;
  std::uint64_t events = 0;
  auto next_snapshot = std::chrono::steady_clock::now() + kSnapshotPeriod;
  while (!stop.stop_requested()) {
    if (std::optional<FeedEvent> e = in.try_pop()) {
      if ((events++ & kSampleMask) == 0) {
        const auto start = std::chrono::steady_clock::now();
        Apply(books, *e);
        metrics.book_apply_ns.Add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start)
                .count()));
        metrics.book_samples.Add();
      } else {
        Apply(books, *e);
      }
      metrics.book_events.Add();
      Forward(out, ToRecord(std::move(*e)));
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Snapshots fire on the wall clock, book activity or not; += keeps the
    // cadence drift-free.
    if (std::chrono::steady_clock::now() >= next_snapshot) {
      for (auto& [id, book] : books) Forward(out, book.OnSnapshot());
      SetGauges(books, metrics);
      next_snapshot += kSnapshotPeriod;
    }
  }
  // The feed thread stops first, so `in` only shrinks here.
  while (std::optional<FeedEvent> e = in.try_pop()) {
    Apply(books, *e);
    metrics.book_events.Add();
    Forward(out, ToRecord(std::move(*e)));
  }
}

}  // namespace nq
