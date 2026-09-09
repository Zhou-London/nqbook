#include <Threads/BookThread.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include <nlib/common.h>
#include <spdlog/spdlog.h>

namespace nq {
namespace {

constexpr std::uint64_t kSampleMask = (1 << 10) - 1;

nlib::record ToRecord(nlib::feed_event &&e) {
  return std::visit([](auto &rec) { return nlib::record(rec); }, e);
}

} // namespace

void BookThread::Run(Metrics &metrics, std::stop_token stop) {
  auto next_snapshot = std::chrono::steady_clock::now() + snapshot_period_;
  while (!stop.stop_requested()) {
    if (std::optional<nlib::feed_event> e = feed_queue_.try_pop()) {
      Consume(std::move(*e), metrics);
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    if (std::chrono::steady_clock::now() >= next_snapshot) {
      Snapshot(metrics);
      next_snapshot += snapshot_period_;
    }
  }
  while (std::optional<nlib::feed_event> e = feed_queue_.try_pop())
    Consume(std::move(*e), metrics);
}

void BookThread::Consume(nlib::feed_event &&e, Metrics &metrics) {
  if ((events_++ & kSampleMask) == 0) {
    const auto start = std::chrono::steady_clock::now();
    Apply(e);
    metrics.book_apply_ns.Add(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start)
            .count()));
    metrics.book_samples.Add();
  } else {
    Apply(e);
  }
  metrics.book_events.Add();
  Forward(ToRecord(std::move(e)));
}

void BookThread::Apply(const nlib::feed_event &e) {
  std::visit(
      nlib::overloaded{[&](const nlib::order &o) {
                         books_[std::uint32_t{o.instrument_id}].OnOrder(o);
                       },
                       [&](const nlib::trade &t) {
                         books_[std::uint32_t{t.instrument_id}].OnTrade(t);
                       },
                       [&](const nlib::cancel &c) {
                         books_[std::uint32_t{c.instrument_id}].OnCancel(c);
                       },
                       [&](const nlib::level &l) {
                         books_[std::uint32_t{l.instrument_id}].OnLevel(l);
                       }},
      e);
}

void BookThread::Forward(nlib::record &&r) {
  if (record_queue_.try_push(std::move(r)))
    return;
  spdlog::warn("record queue full ({} slots)", record_queue_.capacity());
  while (!record_queue_.try_push(std::move(r)))
    std::this_thread::sleep_for(std::chrono::microseconds(50));
}

void BookThread::Snapshot(Metrics &metrics) {
  std::size_t orders = 0;
  std::size_t bytes = 0;
  for (auto &[id, book] : books_) {
    Forward(book.OnSnapshot());
    orders += book.size();
    bytes += book.MemoryBytes();
  }
  metrics.book_instruments.Set(books_.size());
  metrics.book_resting_orders.Set(orders);
  metrics.book_memory_bytes.Set(bytes);
}

} // namespace nq
