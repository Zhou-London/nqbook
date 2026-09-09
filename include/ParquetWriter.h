#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <nlib/common.h>

namespace nq {
class ParquetWriter {
public:
  ParquetWriter(const std::filesystem::path &dir, std::string_view stamp,
                std::int64_t batch_rows);
  ~ParquetWriter();

  ParquetWriter(const ParquetWriter &) = delete;
  ParquetWriter &operator=(const ParquetWriter &) = delete;

  void Add(const nlib::order &o);
  void Add(const nlib::trade &t);
  void Add(const nlib::cancel &c);
  void Add(const nlib::level &l);
  void Add(const nlib::book &b);

  void Close();

private:
  struct Sink;

  static std::unique_ptr<Sink> OpenOrders(const std::filesystem::path &path,
                                          std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenTrades(const std::filesystem::path &path,
                                          std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenCancels(const std::filesystem::path &path,
                                           std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenLevels(const std::filesystem::path &path,
                                          std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenBooks(const std::filesystem::path &path,
                                         std::int64_t batch_rows);

  static void AppendOrder(Sink &sink, const nlib::order &o);

  static void AppendTrade(Sink &sink, const nlib::trade &t);

  static void AppendCancel(Sink &sink, const nlib::cancel &c);

  static void AppendLevel(Sink &sink, const nlib::level &l);

  static void AppendBook(Sink &sink, const nlib::book &b);

  // Creates the builders from sink.schema and opens path for writing.
  static void Open(Sink &sink, const std::filesystem::path &path);

  // Reserves one batch of capacity in every builder.
  static void Reserve(Sink &sink);

  // Counts the appended row and writes the batch once it is full.
  static void Commit(Sink &sink);

  // Writes the buffered rows as one row group.
  static void Flush(Sink &sink);

  std::unique_ptr<Sink> orders_, trades_, cancels_, levels_, books_;
};

} // namespace nq
