#include <Pipeline.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
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

// Snapshot ticks between metrics reports.
constexpr int kMetricsEverySnapshots = 10;

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

// Event counts and sampled apply latency since the last report. Cheap on the
// hot path: one increment per event, one steady_clock pair per sample.
struct ApplyStats {
  std::uint64_t events = 0;
  std::uint64_t samples = 0;
  std::int64_t sample_ns_total = 0;
  std::int64_t sample_ns_max = 0;

  void Note(std::int64_t ns) {
    ++samples;
    sample_ns_total += ns;
    if (ns > sample_ns_max) sample_ns_max = ns;
  }

  // Prints one line of book totals and the window's apply latency, then
  // starts a new window.
  void Report(const nlib::map<std::uint32_t, Orderbook>& books) {
    std::size_t orders = 0;
    std::size_t bytes = 0;
    for (const auto& [id, book] : books) {
      orders += book.size();
      bytes += book.MemoryBytes();
    }
    std::fprintf(stderr,
                 "nqbook book: %zu instruments, %zu resting orders, %.1f MB, "
                 "%llu events (apply avg %lld ns, max %lld ns over %llu samples)\n",
                 books.size(), orders, static_cast<double>(bytes) / (1 << 20),
                 static_cast<unsigned long long>(events),
                 static_cast<long long>(samples ? sample_ns_total / static_cast<std::int64_t>(samples) : 0),
                 static_cast<long long>(sample_ns_max),
                 static_cast<unsigned long long>(samples));
    *this = ApplyStats{};
  }
};

}  // namespace

void RunBook(FeedQueue& in, RecordQueue& out, std::stop_token stop) {
  nlib::map<std::uint32_t, Orderbook> books;
  ApplyStats stats;
  auto next_snapshot = std::chrono::steady_clock::now() + kSnapshotPeriod;
  int snapshots_until_report = kMetricsEverySnapshots;
  while (!stop.stop_requested()) {
    if (std::optional<FeedEvent> e = in.try_pop()) {
      if ((stats.events++ & kSampleMask) == 0) {
        const auto start = std::chrono::steady_clock::now();
        Apply(books, *e);
        stats.Note(std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count());
      } else {
        Apply(books, *e);
      }
      Forward(out, ToRecord(std::move(*e)));
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Snapshots fire on the wall clock, book activity or not; += keeps the
    // cadence drift-free.
    if (std::chrono::steady_clock::now() >= next_snapshot) {
      for (auto& [id, book] : books) Forward(out, book.OnSnapshot());
      next_snapshot += kSnapshotPeriod;
      if (--snapshots_until_report == 0) {
        stats.Report(books);
        snapshots_until_report = kMetricsEverySnapshots;
      }
    }
  }
  // The feed thread stops first, so `in` only shrinks here.
  while (std::optional<FeedEvent> e = in.try_pop()) {
    Apply(books, *e);
    Forward(out, ToRecord(std::move(*e)));
  }
}

}  // namespace nq
