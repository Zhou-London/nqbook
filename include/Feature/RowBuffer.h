#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nq {

class RowBuffer {
public:
    explicit RowBuffer(std::vector<std::string> columns) : columns_(std::move(columns)) {}

    const std::vector<std::string>& Columns() const noexcept { return columns_; }
    std::size_t Ncol() const noexcept { return columns_.size(); }
    std::size_t RowCount() const noexcept { return ts_.size(); }
    bool Empty() const noexcept { return ts_.empty(); }

    void Push(std::int64_t ts, std::span<const double> row) {
        ts_.push_back(ts);
        data_.insert(data_.end(), row.begin(), row.end());
    }

    std::int64_t Ts(std::size_t i) const noexcept { return ts_[i]; }

    std::span<const double> Row(std::size_t i) const noexcept {
        return std::span<const double>(data_.data() + i * columns_.size(), columns_.size());
    }

    double At(std::size_t i, std::size_t j) const noexcept { return data_[i * columns_.size() + j]; }

    std::span<const std::int64_t> Timestamps() const noexcept { return ts_; }
    std::span<const double> Data() const noexcept { return data_; }

    void Clear() noexcept {
        ts_.clear();
        data_.clear();
    }

private:
    std::vector<std::string> columns_;
    std::vector<std::int64_t> ts_;
    std::vector<double> data_;
};

}  // namespace nq
