#include <atomic>
#include <chrono>
#include <csignal>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

#include <Config.h>
#include <Threads/BookThread.h>
#include <Threads/FeedThread.h>
#include <Threads/MetricsThread.h>
#include <Threads/WriterThread.h>

namespace {
// Global signal flag used to terminate the program
std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop.store(true); }

} // namespace

int main() {
  const nq::Config config = nq::LoadConfig("config/config.yaml");
  spdlog::info("[Main]nqbook {} starting", NQBOOK_VERSION);

  spdlog::info("[Config]Book mode={}", config.l2 ? "Level2 Update" : "Market By Order");
  spdlog::info("[Config]Feed subscribes to {}", config.feed_endpoint);
  spdlog::info("[Config]Metrics publishes on {}", config.metrics_endpoint);
  spdlog::info("[Config]Snapshot Period={}ms",
               config.snapshot_period.count());
  spdlog::info("[Config]Writer: Output Path={}",
               config.out_dir.string());
  spdlog::info("[Config]Metrics: Sampling Period={}ms",
               config.sample_period.count());
  spdlog::info("[Config]Cache Line Alignment={}",
               NLIB_CACHE_LINE);

  // Inits SPSC queue between threads
  nq::FeedQueue feed_queue(config.feed_queue_capacity);
  nq::RecordQueue record_queue(config.record_queue_capacity);

  // Inits threads
  nq::FeedThread feed_thread(config.feed_endpoint, config.l2, feed_queue);
  nq::BookThread book_thread(feed_queue, record_queue, config.snapshot_period);
  nq::WriterThread writer_thread(record_queue, config.out_dir,
                                 config.batch_rows);
  nq::MetricsThread metrics_thread(config.metrics_endpoint,
                                   config.sample_period);

  nq::Metrics metrics; // Monitors performances

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  std::jthread tMetrics(
      [&](std::stop_token stop) { metrics_thread.Run(metrics, stop); });
  std::jthread tWriter(
      [&](std::stop_token stop) { writer_thread.Run(metrics, stop); });
  std::jthread tBook(
      [&](std::stop_token stop) { book_thread.Run(metrics, stop); });
  std::jthread tFeed(
      [&](std::stop_token stop) { feed_thread.Run(metrics, stop); });

  spdlog::info("[Main]nqbook up");

  while (!g_stop.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

  spdlog::info("[Main]nqbook stopping");
  tFeed.request_stop();
  tFeed.join();
  tBook.request_stop();
  tBook.join();
  tWriter.request_stop();
  tWriter.join();
  tMetrics.request_stop();
  tMetrics.join();

  spdlog::info("[Main]nqbook down");
  return 0;
}
