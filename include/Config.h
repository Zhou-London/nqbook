#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace nq {
struct Config {
  std::string feed_endpoint = "tcp://host.docker.internal:5555";
  bool l2 = false; // runs the book on level records instead of order flow
  std::size_t feed_queue_capacity = 1 << 14;

  std::chrono::milliseconds snapshot_period{3000};
  std::size_t record_queue_capacity = 1 << 14;

  std::filesystem::path out_dir = "data_out"; // parquet file output path
  std::int64_t batch_rows = 1024;             // parquet writer batch row size

  std::string metrics_endpoint = "tcp://0.0.0.0:5556"; // ZMQ PUB binds here
  std::chrono::milliseconds sample_period{
      100}; // interval between metric samples
};
Config LoadConfig(const std::filesystem::path &path);
} // namespace nq
