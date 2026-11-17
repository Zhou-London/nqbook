#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

namespace nqtest {

struct ParquetFile {
    std::shared_ptr<arrow::Table> table;
    int row_groups;
};

inline ParquetFile ReadParquet(const std::filesystem::path& path) {
    auto file = arrow::io::ReadableFile::Open(path.string()).ValueOrDie();
    auto reader = parquet::arrow::OpenFile(file, arrow::default_memory_pool()).ValueOrDie();
    return {reader->ReadTable().ValueOrDie(), reader->parquet_reader()->metadata()->num_row_groups()};
}

template <class Scalar>
auto ValueAt(const arrow::Table& t, const std::string& column, std::int64_t row) {
    return std::static_pointer_cast<Scalar>(t.GetColumnByName(column)->GetScalar(row).ValueOrDie())->value;
}

inline std::string StringAt(const arrow::Table& t, const std::string& column, std::int64_t row) {
    return t.GetColumnByName(column)->GetScalar(row).ValueOrDie()->ToString();
}

}  // namespace nqtest
