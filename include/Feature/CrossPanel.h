#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <vector>

#include <Feature/Cross.h>

namespace nq {

class CrossPanel {
public:
    static constexpr std::size_t kAlign = 64;
    static constexpr std::uint32_t kInitialRows = 1024;

    CrossPanel(std::size_t nin, std::size_t nout, std::uint32_t capacity = kInitialRows);

    std::size_t Nin() const noexcept { return nin_; }
    std::size_t Nout() const noexcept { return nout_; }
    std::uint32_t RowCount() const noexcept { return nrow_; }
    std::uint32_t Stride() const noexcept { return stride_; }

    double* In(std::size_t column) noexcept { return in_.get() + column * stride_; }
    const double* In(std::size_t column) const noexcept { return in_.get() + column * stride_; }
    const double* Out(std::size_t column) const noexcept { return out_.get() + column * stride_; }

    std::span<const std::uint32_t> Changed() const noexcept { return changed_; }
    std::span<const std::uint32_t> ValidRows() const noexcept { return valid_rows_; }
    bool Valid(std::uint32_t row) const noexcept { return row < nrow_ && ((valid_[row >> 6] >> (row & 63)) & 1); }

    void Touch(std::uint32_t row) {
        if (row >= nrow_) Extend(row + 1);
        std::uint64_t& word = dirty_[row >> 6];
        const std::uint64_t bit = std::uint64_t{1} << (row & 63);
        if (word & bit) return;
        word |= bit;
        changed_.push_back(row);
    }

    void MarkValid(std::uint32_t row) {
        std::uint64_t& word = valid_[row >> 6];
        const std::uint64_t bit = std::uint64_t{1} << (row & 63);
        if (word & bit) return;
        word |= bit;
        valid_rows_.push_back(row);
    }

    void ClearChanged() noexcept {
        for (const std::uint32_t row : changed_) dirty_[row >> 6] &= ~(std::uint64_t{1} << (row & 63));
        changed_.clear();
    }

    void Ensure(std::uint32_t nrow) {
        if (nrow > nrow_) Extend(nrow);
    }

    nq_cross_panel View(std::int64_t ts) noexcept {
        return nq_cross_panel{ts, nrow_, stride_, in_.get(), out_.get(), valid_.data(), changed_.data(),
                              static_cast<std::uint32_t>(changed_.size())};
    }

private:
    struct AlignedDelete {
        void operator()(double* p) const noexcept { ::operator delete[](p, std::align_val_t{kAlign}); }
    };
    using Slab = std::unique_ptr<double[], AlignedDelete>;

    static Slab Allocate(std::size_t ncol, std::uint32_t stride);
    void Extend(std::uint32_t nrow);
    void Reserve(std::uint32_t capacity);

    std::size_t nin_;
    std::size_t nout_;
    std::uint32_t nrow_ = 0;
    std::uint32_t stride_ = 0;
    Slab in_;
    Slab out_;
    std::vector<std::uint64_t> valid_;
    std::vector<std::uint64_t> dirty_;
    std::vector<std::uint32_t> changed_;
    std::vector<std::uint32_t> valid_rows_;
};

}  // namespace nq
