#include <ParquetWriter.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <format>
#include <memory>
#include <utility>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

namespace nq {
namespace {

constexpr std::int32_t kDepth = static_cast<std::int32_t>(nlib::book_depth);

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

// Typed views of a column builder. The caller matches the schema field.
arrow::Int64Builder& I64(const std::shared_ptr<arrow::ArrayBuilder>& b) {
  return static_cast<arrow::Int64Builder&>(*b);
}

arrow::UInt32Builder& U32(const std::shared_ptr<arrow::ArrayBuilder>& b) {
  return static_cast<arrow::UInt32Builder&>(*b);
}

arrow::UInt8Builder& U8(const std::shared_ptr<arrow::ArrayBuilder>& b) {
  return static_cast<arrow::UInt8Builder&>(*b);
}

arrow::FixedSizeListBuilder& List(const std::shared_ptr<arrow::ArrayBuilder>& b) {
  return static_cast<arrow::FixedSizeListBuilder&>(*b);
}

}

struct ParquetWriter::Sink {
  std::shared_ptr<arrow::Schema> schema;
  std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;  // schema order
  std::unique_ptr<parquet::arrow::FileWriter> writer;
  std::int64_t batch_rows = 0;
  std::int64_t rows = 0;
};

ParquetWriter::ParquetWriter(const std::filesystem::path& dir, std::string_view stamp,
                             std::int64_t batch_rows) {
  std::filesystem::create_directories(dir);
  orders_ = OpenOrders(dir / std::format("orders-{}.parquet", stamp), batch_rows);
  trades_ = OpenTrades(dir / std::format("trades-{}.parquet", stamp), batch_rows);
  levels_ = OpenLevels(dir / std::format("levels-{}.parquet", stamp), batch_rows);
  books_ = OpenBooks(dir / std::format("books-{}.parquet", stamp), batch_rows);
}

ParquetWriter::~ParquetWriter() { Close(); }

void ParquetWriter::Add(const nlib::order& o) {
  AppendOrder(*orders_, o);
  Commit(*orders_);
}

void ParquetWriter::Add(const nlib::trade& t) {
  AppendTrade(*trades_, t);
  Commit(*trades_);
}

void ParquetWriter::Add(const nlib::level& l) {
  AppendLevel(*levels_, l);
  Commit(*levels_);
}

void ParquetWriter::Add(const nlib::book& b) {
  AppendBook(*books_, b);
  Commit(*books_);
}

void ParquetWriter::Close() {
  for (std::unique_ptr<Sink>* sink : {&orders_, &trades_, &levels_, &books_}) {
    if (!*sink) continue;
    Flush(**sink);
    Check((*sink)->writer->Close());
    sink->reset();
  }
}

std::unique_ptr<ParquetWriter::Sink> ParquetWriter::OpenOrders(
    const std::filesystem::path& path, std::int64_t batch_rows) {
  auto sink = std::make_unique<Sink>();
  sink->batch_rows = batch_rows;
  sink->schema = arrow::schema({arrow::field("seq", arrow::int64()),
                                arrow::field("order_id", arrow::int64()),
                                arrow::field("price", arrow::int64()),
                                arrow::field("qty", arrow::int64()),
                                arrow::field("cancel_qty", arrow::int64()),
                                arrow::field("new_qty", arrow::int64()),
                                arrow::field("event_ns", arrow::int64()),
                                arrow::field("recv_ns", arrow::int64()),
                                arrow::field("instrument_id", arrow::uint32()),
                                arrow::field("side", arrow::uint8()),
                                arrow::field("type", arrow::uint8()),
                                arrow::field("action", arrow::uint8())});
  Open(*sink, path);
  return sink;
}

std::unique_ptr<ParquetWriter::Sink> ParquetWriter::OpenTrades(
    const std::filesystem::path& path, std::int64_t batch_rows) {
  auto sink = std::make_unique<Sink>();
  sink->batch_rows = batch_rows;
  sink->schema = arrow::schema({arrow::field("seq", arrow::int64()),
                                arrow::field("buy_order_id", arrow::int64()),
                                arrow::field("sell_order_id", arrow::int64()),
                                arrow::field("price", arrow::int64()),
                                arrow::field("qty", arrow::int64()),
                                arrow::field("event_ns", arrow::int64()),
                                arrow::field("recv_ns", arrow::int64()),
                                arrow::field("instrument_id", arrow::uint32()),
                                arrow::field("side", arrow::uint8())});
  Open(*sink, path);
  return sink;
}

std::unique_ptr<ParquetWriter::Sink> ParquetWriter::OpenLevels(
    const std::filesystem::path& path, std::int64_t batch_rows) {
  auto sink = std::make_unique<Sink>();
  sink->batch_rows = batch_rows;
  sink->schema = arrow::schema({arrow::field("seq", arrow::int64()),
                                arrow::field("price", arrow::int64()),
                                arrow::field("qty", arrow::int64()),
                                arrow::field("event_ns", arrow::int64()),
                                arrow::field("recv_ns", arrow::int64()),
                                arrow::field("instrument_id", arrow::uint32()),
                                arrow::field("side", arrow::uint8())});
  Open(*sink, path);
  return sink;
}

std::unique_ptr<ParquetWriter::Sink> ParquetWriter::OpenBooks(
    const std::filesystem::path& path, std::int64_t batch_rows) {
  const auto levels = arrow::fixed_size_list(arrow::int64(), kDepth);
  auto sink = std::make_unique<Sink>();
  sink->batch_rows = batch_rows;
  sink->schema = arrow::schema({arrow::field("event_ns", arrow::int64()),
                                arrow::field("recv_ns", arrow::int64()),
                                arrow::field("instrument_id", arrow::uint32()),
                                arrow::field("bid_price", levels),
                                arrow::field("bid_qty", levels),
                                arrow::field("ask_price", levels),
                                arrow::field("ask_qty", levels)});
  Open(*sink, path);
  return sink;
}

void ParquetWriter::AppendOrder(Sink& sink, const nlib::order& o) {
  const auto& b = sink.builders;
  I64(b[0]).UnsafeAppend(o.seq);
  I64(b[1]).UnsafeAppend(o.order_id);
  I64(b[2]).UnsafeAppend(o.price);
  I64(b[3]).UnsafeAppend(o.qty);
  I64(b[4]).UnsafeAppend(o.cancel_qty);
  I64(b[5]).UnsafeAppend(o.new_qty);
  I64(b[6]).UnsafeAppend(o.event_ns);
  I64(b[7]).UnsafeAppend(o.recv_ns);
  U32(b[8]).UnsafeAppend(o.instrument_id);
  U8(b[9]).UnsafeAppend(std::to_underlying(o.side));
  U8(b[10]).UnsafeAppend(std::to_underlying(o.type));
  U8(b[11]).UnsafeAppend(std::to_underlying(o.action));
}

void ParquetWriter::AppendTrade(Sink& sink, const nlib::trade& t) {
  const auto& b = sink.builders;
  I64(b[0]).UnsafeAppend(t.seq);
  I64(b[1]).UnsafeAppend(t.buy_order_id);
  I64(b[2]).UnsafeAppend(t.sell_order_id);
  I64(b[3]).UnsafeAppend(t.price);
  I64(b[4]).UnsafeAppend(t.qty);
  I64(b[5]).UnsafeAppend(t.event_ns);
  I64(b[6]).UnsafeAppend(t.recv_ns);
  U32(b[7]).UnsafeAppend(t.instrument_id);
  U8(b[8]).UnsafeAppend(std::to_underlying(t.side));
}

void ParquetWriter::AppendLevel(Sink& sink, const nlib::level& l) {
  const auto& b = sink.builders;
  I64(b[0]).UnsafeAppend(l.seq);
  I64(b[1]).UnsafeAppend(l.price);
  I64(b[2]).UnsafeAppend(l.qty);
  I64(b[3]).UnsafeAppend(l.event_ns);
  I64(b[4]).UnsafeAppend(l.recv_ns);
  U32(b[5]).UnsafeAppend(l.instrument_id);
  U8(b[6]).UnsafeAppend(std::to_underlying(l.side));
}

void ParquetWriter::AppendBook(Sink& sink, const nlib::book& b) {
  const auto& builders = sink.builders;
  I64(builders[0]).UnsafeAppend(b.event_ns);
  I64(builders[1]).UnsafeAppend(b.recv_ns);
  U32(builders[2]).UnsafeAppend(b.instrument_id);
  const std::int64_t* const columns[] = {b.bid_price, b.bid_qty, b.ask_price, b.ask_qty};
  for (std::size_t i = 0; i < std::size(columns); ++i) {
    arrow::FixedSizeListBuilder& list = List(builders[3 + i]);
    Check(list.Append());
    auto& values = static_cast<arrow::Int64Builder&>(*list.value_builder());
    for (std::int32_t j = 0; j < kDepth; ++j) values.UnsafeAppend(columns[i][j]);
  }
}

void ParquetWriter::Open(Sink& sink, const std::filesystem::path& path) {
  for (const std::shared_ptr<arrow::Field>& field : sink.schema->fields()) {
    std::unique_ptr<arrow::ArrayBuilder> builder;
    Check(arrow::MakeBuilder(arrow::default_memory_pool(), field->type(), &builder));
    sink.builders.push_back(std::move(builder));
  }
  auto file = Check(arrow::io::FileOutputStream::Open(path.string()));
  auto properties =
      parquet::WriterProperties::Builder().compression(arrow::Compression::ZSTD)->build();
  auto arrow_properties = parquet::ArrowWriterProperties::Builder().store_schema()->build();
  sink.writer = Check(parquet::arrow::FileWriter::Open(
      *sink.schema, arrow::default_memory_pool(), std::move(file), std::move(properties),
      std::move(arrow_properties)));
  Reserve(sink);
}

void ParquetWriter::Reserve(Sink& sink) {
  for (const std::shared_ptr<arrow::ArrayBuilder>& builder : sink.builders) {
    Check(builder->Reserve(sink.batch_rows));
    // A fixed-size list column holds kDepth values per row.
    if (builder->type()->id() == arrow::Type::FIXED_SIZE_LIST)
      Check(List(builder).value_builder()->Reserve(sink.batch_rows * kDepth));
  }
}

void ParquetWriter::Commit(Sink& sink) {
  if (++sink.rows == sink.batch_rows) Flush(sink);
}

void ParquetWriter::Flush(Sink& sink) {
  if (sink.rows == 0) return;
  std::vector<std::shared_ptr<arrow::Array>> columns(sink.builders.size());
  for (std::size_t i = 0; i < columns.size(); ++i)
    Check(sink.builders[i]->Finish(&columns[i]));
  Check(sink.writer->WriteRecordBatch(*arrow::RecordBatch::Make(
      sink.schema, std::exchange(sink.rows, 0), std::move(columns))));
  Reserve(sink);
}

}
