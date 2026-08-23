#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <nlib/common.h>

namespace nq {

// Writes one zstd-compressed Parquet file per record type. Every Arrow and
// Parquet call of the service lives here. A storage error aborts the process:
// running on without persistence loses records silently.
class ParquetWriter {
 public:
  // Creates dir and opens the orders, trades, levels, and books files, each
  // named with stamp. Every file buffers batch_rows rows per row group.
  ParquetWriter(const std::filesystem::path& dir, std::string_view stamp,
                std::int64_t batch_rows);

  // Calls Close().
  ~ParquetWriter();

  ParquetWriter(const ParquetWriter&) = delete;
  ParquetWriter& operator=(const ParquetWriter&) = delete;

  // Buffers one row; a full batch goes out as one row group. Call before
  // Close().
  void Add(const nlib::order& o);

  void Add(const nlib::trade& t);

  void Add(const nlib::level& l);

  void Add(const nlib::book& b);

  // Writes the buffered rows and closes every file. Later calls do nothing.
  void Close();

 private:
  // One open Parquet file: its schema, one builder per column in schema
  // order, the file writer, the batch size, and the rows buffered since the
  // last row group.
  struct Sink;

  // Opens the file of one record type. Each pairs with the Append function
  // below: both list the columns in the same order.
  static std::unique_ptr<Sink> OpenOrders(const std::filesystem::path& path,
                                        std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenTrades(const std::filesystem::path& path,
                                        std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenLevels(const std::filesystem::path& path,
                                        std::int64_t batch_rows);

  static std::unique_ptr<Sink> OpenBooks(const std::filesystem::path& path,
                                        std::int64_t batch_rows);

  // Appends one row to the builders, column by column.
  static void AppendOrder(Sink& sink, const nlib::order& o);

  static void AppendTrade(Sink& sink, const nlib::trade& t);

  static void AppendLevel(Sink& sink, const nlib::level& l);

  static void AppendBook(Sink& sink, const nlib::book& b);

  // Creates the builders from sink.schema and opens path for writing.
  static void Open(Sink& sink, const std::filesystem::path& path);

  // Reserves one batch of capacity in every builder.
  static void Reserve(Sink& sink);

  // Counts the appended row and writes the batch once it is full.
  static void Commit(Sink& sink);

  // Writes the buffered rows as one row group.
  static void Flush(Sink& sink);

  std::unique_ptr<Sink> orders_, trades_, levels_, books_;
};

}
