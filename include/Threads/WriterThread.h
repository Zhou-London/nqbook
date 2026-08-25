#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>

#include <ParquetWriter.h>
#include <Threads/BookThread.h>
#include <Threads/MetricsThread.h>
#include <nlib/common.h>

namespace nq {

// The writer stage: hands each record to a nq::ParquetWriter, which holds one
// file per record type. This stage moves records and counts rows; every Arrow
// and Parquet call lives in ParquetWriter.
class WriterThread {
public:
  // Drains in into files under dir, buffering batch_rows rows per row group.
  WriterThread(RecordQueue &in, std::filesystem::path dir, std::int64_t batch_rows);
  ~WriterThread();

  // Writes records until stop is requested, then drains in, flushes the
  // partial batches, and closes the files. A storage error aborts the
  // process, because running on without persistence loses records silently.
  void Run(Metrics &metrics, std::stop_token stop);

private:
  // Opens the files, named with the writer start time.
  void Open();

  // Adds one record to its file and counts the row.
  void Consume(nlib::record &&r, Metrics &metrics);

  // Flushes and closes every file.
  void Close();

  RecordQueue &record_queue_;
  std::filesystem::path dir_;
  std::int64_t batch_rows_;
  std::unique_ptr<ParquetWriter> writer_;
};

}
