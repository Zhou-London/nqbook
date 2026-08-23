#include <Threads/WriterThread.h>

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace nq {

WriterThread::WriterThread(RecordQueue& in, std::filesystem::path dir,
                           std::int64_t batch_rows)
    : record_queue_(in), dir_(std::move(dir)), batch_rows_(batch_rows) {}

WriterThread::~WriterThread() = default;

void WriterThread::Run(Metrics& metrics, std::stop_token stop) {
  Open();
  while (!stop.stop_requested()) {
    if (std::optional<nlib::record> r = record_queue_.try_pop())
      Consume(std::move(*r), metrics);
    else
      std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  while (std::optional<nlib::record> r = record_queue_.try_pop())
    Consume(std::move(*r), metrics);
  Close();
}

void WriterThread::Open() {
  const std::string stamp = std::format(
      "{:%Y%m%dT%H%M%S}",
      std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  writer_ = std::make_unique<ParquetWriter>(dir_, stamp, batch_rows_);
}

void WriterThread::Consume(nlib::record&& r, Metrics& metrics) {
  std::visit(nlib::overloaded{[&](const nlib::order& o) {
                                writer_->Add(o);
                                metrics.writer_orders.Add();
                              },
                              [&](const nlib::trade& t) {
                                writer_->Add(t);
                                metrics.writer_trades.Add();
                              },
                              [&](const nlib::level& l) {
                                writer_->Add(l);
                                metrics.writer_levels.Add();
                              },
                              [&](const nlib::book& b) {
                                writer_->Add(b);
                                metrics.writer_books.Add();
                              }},
             r);
}

void WriterThread::Close() { writer_->Close(); }

}
