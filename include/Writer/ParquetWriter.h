#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include <nq/instrument.hpp>

#include <Defaults.h>
#include <Feature/FeatureCatalog.h>
#include <Writer/RowSink.h>

namespace nq {

struct ParquetOptions {
    std::int64_t row_group_rows = defaults::kRowGroupRows;
};

class ParquetWriter {
public:
    ParquetWriter(const FeatureCatalog& catalog, const std::filesystem::path& dir, ParquetOptions options = {});
    ~ParquetWriter();

    ParquetWriter(const ParquetWriter&) = delete;
    ParquetWriter& operator=(const ParquetWriter&) = delete;

    void Register(const Instrument& instrument);
    void Write(const FeatureBlock& block);
    void Close();

    std::size_t TableCount() const noexcept { return tables_.size(); }
    const std::filesystem::path& Path(std::size_t table) const noexcept;
    std::uint64_t Rows(std::size_t table) const noexcept;
    std::vector<std::filesystem::path> Files() const;
    bool Closed() const noexcept { return closed_; }

private:
    struct Table;

    void Flush(Table& t);
    void WriteInstruments();

    ParquetOptions options_;
    std::vector<std::unique_ptr<Table>> tables_;
    std::unordered_map<std::uint32_t, Instrument> instruments_;
    std::filesystem::path instruments_path_;
    bool instruments_written_ = false;
    bool closed_ = false;
};

}  // namespace nq
