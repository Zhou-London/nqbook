#include <Config.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <fkYAML/node.hpp>

namespace nq {
namespace {

// Assigns map[key] to field, leaving the default in place when the key is absent.
template <typename T>
void Read(const fkyaml::node& map, const char* key, T& field) {
  if (map.contains(key)) field = map[key].get_value<T>();
}

void Read(const fkyaml::node& map, const char* key, std::chrono::milliseconds& field) {
  if (map.contains(key)) field = std::chrono::milliseconds(map[key].get_value<std::int64_t>());
}

void Read(const fkyaml::node& map, const char* key, std::filesystem::path& field) {
  if (map.contains(key)) field = map[key].get_value<std::string>();
}

}

Config LoadConfig(const std::filesystem::path& path) {
  Config config;
  if (!std::filesystem::exists(path)) return config;
  try {
    std::ifstream in(path);
    const fkyaml::node root = fkyaml::node::deserialize(in);
    if (root.contains("feed")) {
      const fkyaml::node& feed = root["feed"];
      Read(feed, "endpoint", config.feed_endpoint);
      Read(feed, "queue_capacity", config.feed_queue_capacity);
    }
    if (root.contains("book")) {
      const fkyaml::node& book = root["book"];
      Read(book, "snapshot_period_ms", config.snapshot_period);
      Read(book, "queue_capacity", config.record_queue_capacity);
    }
    if (root.contains("writer")) {
      const fkyaml::node& writer = root["writer"];
      Read(writer, "out_dir", config.out_dir);
      Read(writer, "batch_rows", config.batch_rows);
    }
    if (root.contains("metrics")) {
      const fkyaml::node& metrics = root["metrics"];
      Read(metrics, "endpoint", config.metrics_endpoint);
      Read(metrics, "sample_period_ms", config.sample_period);
    }
  } catch (const fkyaml::exception& e) {
    std::fprintf(stderr, "nqbook config: %s: %s\n", path.string().c_str(), e.what());
    std::abort();
  }
  // A batch of no rows leaves the writer's builders unreserved.
  if (config.batch_rows < 1) {
    std::fprintf(stderr, "nqbook config: %s: writer.batch_rows is below 1\n",
                 path.string().c_str());
    std::abort();
  }
  return config;
}

}
