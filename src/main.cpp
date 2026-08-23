#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <stop_token>
#include <string_view>
#include <thread>

#include <Config.h>
#include <Threads/BookThread.h>
#include <Threads/FeedThread.h>
#include <Threads/MetricsThread.h>
#include <Threads/WriterThread.h>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true); }

}

int main(int argc, char** argv) {
  const char* config_path = std::getenv("NQBOOK_CONFIG");
  nq::Config config = nq::LoadConfig(config_path ? config_path : "config/config.yaml");
  if (const char* v = std::getenv("NQBOOK_FEED_ENDPOINT")) config.feed_endpoint = v;
  if (const char* v = std::getenv("NQBOOK_METRICS_ENDPOINT")) config.metrics_endpoint = v;

  // Parse command line arguments
  bool l2 = false;
  for (int i = 1; i < argc;) {
    if (std::string_view(argv[i]) == "--l2") {
      l2 = true;
      for (int j = i; j + 1 < argc; ++j) argv[j] = argv[j + 1];
      --argc;
    } else {
      ++i;
    }
  }
  if (argc > 1) config.feed_endpoint = argv[1];
  if (argc > 2) config.out_dir = argv[2];
  if (argc > 3) config.metrics_endpoint = argv[3];

  nq::FeedQueue feed_queue(config.feed_queue_capacity);
  nq::RecordQueue record_queue(config.record_queue_capacity);
  nq::Metrics metrics;
  nq::FeedThread feed_stage(config.feed_endpoint, l2, feed_queue);
  nq::BookThread book_stage(feed_queue, record_queue, config.snapshot_period);
  nq::WriterThread writer_stage(record_queue, config.out_dir, config.batch_rows);
  nq::MetricsThread metrics_stage(config.metrics_endpoint, config.sample_period);

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  std::jthread monitor(
      [&](std::stop_token stop) { metrics_stage.Run(metrics, stop); });
  std::jthread writer(
      [&](std::stop_token stop) { writer_stage.Run(metrics, stop); });
  std::jthread book(
      [&](std::stop_token stop) { book_stage.Run(metrics, stop); });
  std::jthread feed(
      [&](std::stop_token stop) { feed_stage.Run(metrics, stop); });

  std::printf("nqbook: feed %s%s, writing %s, metrics %s\n", config.feed_endpoint.c_str(),
              l2 ? " (l2)" : "", config.out_dir.string().c_str(),
              config.metrics_endpoint.c_str());
  while (!g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));

  feed.request_stop();
  feed.join();
  book.request_stop();
  book.join();
  writer.request_stop();
  writer.join();
  monitor.request_stop();
  monitor.join();
  std::printf("nqbook: stopped\n");
  return 0;
}
