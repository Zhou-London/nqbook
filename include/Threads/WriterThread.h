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

class WriterThread {
public:
  WriterThread(RecordQueue &in, std::filesystem::path dir, std::int64_t batch_rows);
  ~WriterThread();

  void Run(Metrics &metrics, std::stop_token stop);

private:
  void Open();

  void Consume(nlib::record &&r, Metrics &metrics);

  void Close();

  RecordQueue &record_queue_;
  std::filesystem::path dir_;
  std::int64_t batch_rows_;
  std::unique_ptr<ParquetWriter> writer_;
};

}
