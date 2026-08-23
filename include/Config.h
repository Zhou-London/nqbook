#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace nq {

// Stores the configuration from config.yaml
struct Config {
  std::string feed_endpoint = "tcp://host.docker.internal:5555";
  std::size_t feed_queue_capacity = 1 << 14;

  std::chrono::milliseconds snapshot_period{3000};
  std::size_t record_queue_capacity = 1 << 14;

  std::filesystem::path out_dir = "data_out";
  std::int64_t batch_rows = 1024;

  std::string metrics_endpoint = "tcp://0.0.0.0:5556";
  std::chrono::milliseconds sample_period{100};
};

// Reads the config.yaml
Config LoadConfig(const std::filesystem::path &path);

}
