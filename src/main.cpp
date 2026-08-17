// nqbook service entry point: starts the feed, book, and writer threads and
// runs until SIGINT or SIGTERM. Shutdown is upstream-first — feed, then book,
// then writer — so each stage drains its input and every received record
// reaches the Parquet files.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stop_token>
#include <string>
#include <thread>

#include <Pipeline.h>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) { g_stop.store(true); }

}  // namespace

// Usage: nqbook [feed_endpoint] [out_dir]. The endpoint falls back to
// NQBOOK_FEED_ENDPOINT, then to the host-gateway default for a feed running
// on the docker host.
int main(int argc, char** argv) {
  const char* env_endpoint = std::getenv("NQBOOK_FEED_ENDPOINT");
  const std::string endpoint = argc > 1      ? argv[1]
                               : env_endpoint ? env_endpoint
                                              : "tcp://host.docker.internal:5555";
  const std::filesystem::path out_dir = argc > 2 ? argv[2] : "data_out";

  nq::FeedQueue feed_queue(1 << 14);
  nq::RecordQueue record_queue(1 << 14);

  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);

  std::jthread writer(
      [&](std::stop_token stop) { nq::RunWriter(record_queue, out_dir, stop); });
  std::jthread book([&](std::stop_token stop) { nq::RunBook(feed_queue, record_queue, stop); });
  std::jthread feed([&](std::stop_token stop) { nq::RunFeed(endpoint, feed_queue, stop); });

  std::printf("nqbook: feed %s, writing %s\n", endpoint.c_str(), out_dir.string().c_str());
  while (!g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));

  feed.request_stop();
  feed.join();
  book.request_stop();
  book.join();
  writer.request_stop();
  writer.join();
  std::printf("nqbook: stopped\n");
  return 0;
}
