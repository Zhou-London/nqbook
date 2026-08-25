#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace nq {

// Every tunable of the service, holding the value it ships with. A key left
// out of config.yaml keeps the default named here.
struct Config {
  std::string feed_endpoint = "tcp://host.docker.internal:5555";  // ZMQ SUB connects here
  std::size_t feed_queue_capacity = 1 << 14;  // events queued ahead of the book stage

  std::chrono::milliseconds snapshot_period{3000};  // interval between book snapshots
  std::size_t record_queue_capacity = 1 << 14;  // records queued ahead of the writer stage

  std::filesystem::path out_dir = "data_out";  // holds the Parquet files, created when missing
  std::int64_t batch_rows = 1024;  // rows buffered per file before a row group goes out

  std::string metrics_endpoint = "tcp://0.0.0.0:5556";  // ZMQ PUB binds here
  std::chrono::milliseconds sample_period{100};  // interval between metric samples
};

// Reads the tunables path names and defaults the rest. A missing file leaves
// every default in place; a malformed file or a batch_rows below 1 prints the
// reason and aborts, so a bad configuration stops the process at startup.
Config LoadConfig(const std::filesystem::path &path);

}
