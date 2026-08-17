// Each record type gets its own sink: an Arrow schema, one builder per
// column, and a parquet::arrow::FileWriter. A sink buffers kBatchRows rows,
// then writes them as one row group. Builder capacity is reserved a batch at
// a time, so the per-row path is UnsafeAppend only.
#include <Pipeline.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

namespace nq {
namespace {

// Rows per Arrow batch and per Parquet row group.
constexpr std::int64_t kBatchRows = 1024;

constexpr std::int32_t kDepth = static_cast<std::int32_t>(nlib::book_depth);

// The writer is the persistence stage; continuing without it would silently
// lose data, so any storage error is fatal.
void Check(const arrow::Status& status) {
  if (status.ok()) return;
  std::fprintf(stderr, "nqbook writer: %s\n", status.ToString().c_str());
  std::abort();
}

template <typename T>
T Check(arrow::Result<T> result) {
  Check(result.status());
  return std::move(result).MoveValueUnsafe();
}

// Opens `path` for writing as zstd-compressed Parquet. store_schema() embeds
// the Arrow schema so readers restore fixed-size lists instead of plain ones.
std::unique_ptr<parquet::arrow::FileWriter> MakeWriter(const std::filesystem::path& path,
                                                       const arrow::Schema& schema) {
  auto file = Check(arrow::io::FileOutputStream::Open(path.string()));
  auto properties =
      parquet::WriterProperties::Builder().compression(arrow::Compression::ZSTD)->build();
  auto arrow_properties = parquet::ArrowWriterProperties::Builder().store_schema()->build();
  return Check(parquet::arrow::FileWriter::Open(schema, arrow::default_memory_pool(),
                                                std::move(file), std::move(properties),
                                                std::move(arrow_properties)));
}

// Sink for forwarded nlib::order records. `side`/`type`/`action` are stored
// as the enums' underlying codes; `prev`/`next` are book-internal and not
// stored.
class OrderSink {
 public:
  explicit OrderSink(const std::filesystem::path& path)
      : schema_(arrow::schema({arrow::field("seq", arrow::int64()),
                               arrow::field("order_id", arrow::int64()),
                               arrow::field("price", arrow::int64()),
                               arrow::field("qty", arrow::int64()),
                               arrow::field("time_ns", arrow::int64()),
                               arrow::field("instrument_id", arrow::uint32()),
                               arrow::field("side", arrow::uint8()),
                               arrow::field("type", arrow::uint8()),
                               arrow::field("action", arrow::uint8())})),
        writer_(MakeWriter(path, *schema_)) {
    Reserve();
  }

  void Add(const nlib::order& o) {
    seq_.UnsafeAppend(o.seq);
    order_id_.UnsafeAppend(o.order_id);
    price_.UnsafeAppend(o.price);
    qty_.UnsafeAppend(o.qty);
    time_ns_.UnsafeAppend(o.time_ns);
    instrument_id_.UnsafeAppend(o.instrument_id);
    side_.UnsafeAppend(std::to_underlying(o.side));
    type_.UnsafeAppend(std::to_underlying(o.type));
    action_.UnsafeAppend(std::to_underlying(o.action));
    if (++rows_ == kBatchRows) Flush();
  }

  void Flush() {
    if (rows_ == 0) return;
    std::vector<std::shared_ptr<arrow::Array>> columns(9);
    Check(seq_.Finish(&columns[0]));
    Check(order_id_.Finish(&columns[1]));
    Check(price_.Finish(&columns[2]));
    Check(qty_.Finish(&columns[3]));
    Check(time_ns_.Finish(&columns[4]));
    Check(instrument_id_.Finish(&columns[5]));
    Check(side_.Finish(&columns[6]));
    Check(type_.Finish(&columns[7]));
    Check(action_.Finish(&columns[8]));
    Check(writer_->WriteRecordBatch(
        *arrow::RecordBatch::Make(schema_, std::exchange(rows_, 0), std::move(columns))));
    Reserve();
  }

  void Close() {
    Flush();
    Check(writer_->Close());
  }

 private:
  void Reserve() {
    for (arrow::ArrayBuilder* b :
         std::initializer_list<arrow::ArrayBuilder*>{&seq_, &order_id_, &price_, &qty_,
                                                     &time_ns_, &instrument_id_, &side_, &type_,
                                                     &action_})
      Check(b->Reserve(kBatchRows));
  }

  std::shared_ptr<arrow::Schema> schema_;
  std::unique_ptr<parquet::arrow::FileWriter> writer_;
  arrow::Int64Builder seq_, order_id_, price_, qty_, time_ns_;
  arrow::UInt32Builder instrument_id_;
  arrow::UInt8Builder side_, type_, action_;
  std::int64_t rows_ = 0;  // rows in the open batch
};

// Sink for forwarded nlib::trade records; `side` is the aggressor side's code.
class TradeSink {
 public:
  explicit TradeSink(const std::filesystem::path& path)
      : schema_(arrow::schema({arrow::field("seq", arrow::int64()),
                               arrow::field("buy_order_id", arrow::int64()),
                               arrow::field("sell_order_id", arrow::int64()),
                               arrow::field("price", arrow::int64()),
                               arrow::field("qty", arrow::int64()),
                               arrow::field("time_ns", arrow::int64()),
                               arrow::field("instrument_id", arrow::uint32()),
                               arrow::field("side", arrow::uint8())})),
        writer_(MakeWriter(path, *schema_)) {
    Reserve();
  }

  void Add(const nlib::trade& t) {
    seq_.UnsafeAppend(t.seq);
    buy_order_id_.UnsafeAppend(t.buy_order_id);
    sell_order_id_.UnsafeAppend(t.sell_order_id);
    price_.UnsafeAppend(t.price);
    qty_.UnsafeAppend(t.qty);
    time_ns_.UnsafeAppend(t.time_ns);
    instrument_id_.UnsafeAppend(t.instrument_id);
    side_.UnsafeAppend(std::to_underlying(t.side));
    if (++rows_ == kBatchRows) Flush();
  }

  void Flush() {
    if (rows_ == 0) return;
    std::vector<std::shared_ptr<arrow::Array>> columns(8);
    Check(seq_.Finish(&columns[0]));
    Check(buy_order_id_.Finish(&columns[1]));
    Check(sell_order_id_.Finish(&columns[2]));
    Check(price_.Finish(&columns[3]));
    Check(qty_.Finish(&columns[4]));
    Check(time_ns_.Finish(&columns[5]));
    Check(instrument_id_.Finish(&columns[6]));
    Check(side_.Finish(&columns[7]));
    Check(writer_->WriteRecordBatch(
        *arrow::RecordBatch::Make(schema_, std::exchange(rows_, 0), std::move(columns))));
    Reserve();
  }

  void Close() {
    Flush();
    Check(writer_->Close());
  }

 private:
  void Reserve() {
    for (arrow::ArrayBuilder* b :
         std::initializer_list<arrow::ArrayBuilder*>{&seq_, &buy_order_id_, &sell_order_id_,
                                                     &price_, &qty_, &time_ns_, &instrument_id_,
                                                     &side_})
      Check(b->Reserve(kBatchRows));
  }

  std::shared_ptr<arrow::Schema> schema_;
  std::unique_ptr<parquet::arrow::FileWriter> writer_;
  arrow::Int64Builder seq_, buy_order_id_, sell_order_id_, price_, qty_, time_ns_;
  arrow::UInt32Builder instrument_id_;
  arrow::UInt8Builder side_;
  std::int64_t rows_ = 0;  // rows in the open batch
};

// Sink for nlib::book snapshots. Each per-side array is one
// fixed_size_list<int64>[book_depth] column, best level first. Per row only
// the child values are appended; the lists' validity is appended in bulk at
// flush, which is what keeps the row path Status-free.
class BookSink {
 public:
  explicit BookSink(const std::filesystem::path& path)
      : schema_(arrow::schema({arrow::field("time_ns", arrow::int64()),
                               arrow::field("instrument_id", arrow::uint32()),
                               arrow::field("bid_price", arrow::fixed_size_list(arrow::int64(), kDepth)),
                               arrow::field("bid_qty", arrow::fixed_size_list(arrow::int64(), kDepth)),
                               arrow::field("ask_price", arrow::fixed_size_list(arrow::int64(), kDepth)),
                               arrow::field("ask_qty", arrow::fixed_size_list(arrow::int64(), kDepth))})),
        writer_(MakeWriter(path, *schema_)) {
    Reserve();
  }

  void Add(const nlib::book& b) {
    time_ns_.UnsafeAppend(b.time_ns);
    instrument_id_.UnsafeAppend(b.instrument_id);
    for (std::int32_t i = 0; i < kDepth; ++i) {
      bid_price_->UnsafeAppend(b.bid_price[i]);
      bid_qty_->UnsafeAppend(b.bid_qty[i]);
      ask_price_->UnsafeAppend(b.ask_price[i]);
      ask_qty_->UnsafeAppend(b.ask_qty[i]);
    }
    if (++rows_ == kBatchRows) Flush();
  }

  void Flush() {
    if (rows_ == 0) return;
    for (arrow::FixedSizeListBuilder* lists :
         {&bid_price_lists_, &bid_qty_lists_, &ask_price_lists_, &ask_qty_lists_})
      Check(lists->AppendValues(rows_));  // marks the batch's lists valid in one call
    std::vector<std::shared_ptr<arrow::Array>> columns(6);
    Check(time_ns_.Finish(&columns[0]));
    Check(instrument_id_.Finish(&columns[1]));
    Check(bid_price_lists_.Finish(&columns[2]));
    Check(bid_qty_lists_.Finish(&columns[3]));
    Check(ask_price_lists_.Finish(&columns[4]));
    Check(ask_qty_lists_.Finish(&columns[5]));
    Check(writer_->WriteRecordBatch(
        *arrow::RecordBatch::Make(schema_, std::exchange(rows_, 0), std::move(columns))));
    Reserve();
  }

  void Close() {
    Flush();
    Check(writer_->Close());
  }

 private:
  void Reserve() {
    Check(time_ns_.Reserve(kBatchRows));
    Check(instrument_id_.Reserve(kBatchRows));
    for (arrow::FixedSizeListBuilder* lists :
         {&bid_price_lists_, &bid_qty_lists_, &ask_price_lists_, &ask_qty_lists_})
      Check(lists->Reserve(kBatchRows));
    for (const std::shared_ptr<arrow::Int64Builder>& values :
         {bid_price_, bid_qty_, ask_price_, ask_qty_})
      Check(values->Reserve(kBatchRows * kDepth));
  }

  std::shared_ptr<arrow::Schema> schema_;
  std::unique_ptr<parquet::arrow::FileWriter> writer_;
  arrow::Int64Builder time_ns_;
  arrow::UInt32Builder instrument_id_;
  std::shared_ptr<arrow::Int64Builder> bid_price_ = std::make_shared<arrow::Int64Builder>();
  std::shared_ptr<arrow::Int64Builder> bid_qty_ = std::make_shared<arrow::Int64Builder>();
  std::shared_ptr<arrow::Int64Builder> ask_price_ = std::make_shared<arrow::Int64Builder>();
  std::shared_ptr<arrow::Int64Builder> ask_qty_ = std::make_shared<arrow::Int64Builder>();
  arrow::FixedSizeListBuilder bid_price_lists_{arrow::default_memory_pool(), bid_price_,
                                               arrow::fixed_size_list(arrow::int64(), kDepth)};
  arrow::FixedSizeListBuilder bid_qty_lists_{arrow::default_memory_pool(), bid_qty_,
                                             arrow::fixed_size_list(arrow::int64(), kDepth)};
  arrow::FixedSizeListBuilder ask_price_lists_{arrow::default_memory_pool(), ask_price_,
                                               arrow::fixed_size_list(arrow::int64(), kDepth)};
  arrow::FixedSizeListBuilder ask_qty_lists_{arrow::default_memory_pool(), ask_qty_,
                                             arrow::fixed_size_list(arrow::int64(), kDepth)};
  std::int64_t rows_ = 0;  // rows in the open batch
};

}  // namespace

void RunWriter(RecordQueue& in, const std::filesystem::path& dir, std::stop_token stop) {
  std::filesystem::create_directories(dir);
  const auto stamp = std::format(
      "{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(
                              std::chrono::system_clock::now()));
  OrderSink orders(dir / std::format("orders-{}.parquet", stamp));
  TradeSink trades(dir / std::format("trades-{}.parquet", stamp));
  BookSink books(dir / std::format("books-{}.parquet", stamp));

  const auto dispatch = [&](Record&& r) {
    std::visit(overloaded{[&](const nlib::order& o) { orders.Add(o); },
                          [&](const nlib::trade& t) { trades.Add(t); },
                          [&](const nlib::book& b) { books.Add(b); }},
               r);
  };
  while (!stop.stop_requested()) {
    if (std::optional<Record> r = in.try_pop())
      dispatch(std::move(*r));
    else
      std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  // The book thread stops first, so `in` only shrinks here.
  while (std::optional<Record> r = in.try_pop()) dispatch(std::move(*r));
  orders.Close();
  trades.Close();
  books.Close();
}

}  // namespace nq
