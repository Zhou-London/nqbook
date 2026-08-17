// nqbook service entry point: starts the metrics, writer, book, and feed
// threads and runs until SIGINT or SIGTERM. Shutdown is upstream-first —
// feed, then book, then writer, then metrics — so each stage drains its input
// and every received record reaches the Parquet files.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stop_token>
#include <string>
#include <thread>

#include <Metrics.h>
#include <Pipeline.h>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true); }

}  // namespace

// Usage: nqbook [feed_endpoint] [out_dir] [metrics_endpoint]. The feed
// endpoint falls back to NQBOOK_FEED_ENDPOINT, then to the host-gateway
// default for a feed running on the docker host; the metrics endpoint to
// NQBOOK_METRICS_ENDPOINT, then to binding port 5556 on every interface.
int main(int argc, char** argv) {
  const char* env_endpoint = std::getenv("NQBOOK_FEED_ENDPOINT");
  const std::string endpoint = argc > 1      ? argv[1]
                               : env_endpoint ? env_endpoint
                                              : "tcp://host.docker.internal:5555";
  const std::filesystem::path out_dir = argc > 2 ? argv[2] : "data_out";
  const char* env_metrics = std::getenv("NQBOOK_METRICS_ENDPOINT");
  const std::string metrics_endpoint = argc > 3      ? argv[3]
                                       : env_metrics ? env_metrics
                                                     : "tcp://0.0.0.0:5556";

  nq::FeedQueue feed_queue(1 << 14);
  nq::RecordQueue record_queue(1 << 14);
  nq::Metrics metrics;

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  std::jthread monitor(
      [&](std::stop_token stop) { nq::RunMetrics(metrics, metrics_endpoint, stop); });
  std::jthread writer(
      [&](std::stop_token stop) { nq::RunWriter(record_queue, out_dir, metrics, stop); });
  std::jthread book([&](std::stop_token stop) {
    nq::RunBook(feed_queue, record_queue, metrics, stop);
  });
  std::jthread feed([&](std::stop_token stop) {
    nq::RunFeed(endpoint, feed_queue, metrics, stop);
  });

  std::printf("nqbook: feed %s, writing %s, metrics %s\n", endpoint.c_str(),
              out_dir.string().c_str(), metrics_endpoint.c_str());
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
