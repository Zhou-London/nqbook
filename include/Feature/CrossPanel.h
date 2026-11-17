#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <vector>

#include <nq/feature_abi.h>

#include <Bitmap.h>

namespace nq {

class RowSet {
public:
    void Reserve(std::uint32_t rows) {
        members_.Resize(rows);
        rows_.reserve(rows);
    }

    bool Contains(std::uint32_t row) const noexcept { return members_.Test(row); }
    void Insert(std::uint32_t row) {
        if (!members_.TestAndSet(row)) rows_.push_back(row);
    }
    void Clear() noexcept {
        for (const std::uint32_t row : rows_) members_.Clear(row);
        rows_.clear();
    }

    std::span<const std::uint32_t> Rows() const noexcept { return rows_; }
    const std::uint64_t* Words() const noexcept { return members_.Words(); }

private:
    Bitmap members_;
    std::vector<std::uint32_t> rows_;
};

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

    std::span<const std::uint32_t> Changed() const noexcept { return changed_.Rows(); }
    std::span<const std::uint32_t> ValidRows() const noexcept { return valid_.Rows(); }
    bool Valid(std::uint32_t row) const noexcept { return valid_.Contains(row); }

    void Touch(std::uint32_t row) { changed_.Insert(row); }
    void MarkValid(std::uint32_t row) { valid_.Insert(row); }
    void ClearChanged() noexcept { changed_.Clear(); }

    void Ensure(std::uint32_t nrow);

    nq_cross_panel View(std::int64_t ts) noexcept {
        const std::span<const std::uint32_t> changed = Changed();
        return nq_cross_panel{ts, nrow_, stride_, in_.get(), out_.get(), valid_.Words(), changed.data(),
                              static_cast<std::uint32_t>(changed.size())};
    }

private:
    struct AlignedDelete {
        void operator()(double* p) const noexcept { ::operator delete[](p, std::align_val_t{kAlign}); }
    };
    using Slab = std::unique_ptr<double[], AlignedDelete>;

    static Slab Allocate(std::size_t ncol, std::uint32_t stride);
    void Reserve(std::uint32_t capacity);

    std::size_t nin_;
    std::size_t nout_;
    std::uint32_t nrow_ = 0;
    std::uint32_t stride_ = 0;
    Slab in_;
    Slab out_;
    RowSet valid_;
    RowSet changed_;
};

}  // namespace nq
