#include <Feature/CrossPanel.h>

#include <algorithm>
#include <limits>

namespace nq {

CrossPanel::CrossPanel(std::size_t nin, std::size_t nout, std::uint32_t capacity) : nin_(nin), nout_(nout) {
    Reserve(capacity);
}

CrossPanel::Slab CrossPanel::Allocate(std::size_t ncol, std::uint32_t stride) {
    const std::size_t n = ncol * stride;
    if (n == 0) return Slab();
    Slab slab(static_cast<double*>(::operator new[](n * sizeof(double), std::align_val_t{kAlign})));
    std::fill_n(slab.get(), n, std::numeric_limits<double>::quiet_NaN());
    return slab;
}

void CrossPanel::Ensure(std::uint32_t nrow) {
    if (nrow <= nrow_) return;
    if (nrow > stride_) Reserve(std::max(nrow, stride_ * 2));
    nrow_ = nrow;
}

void CrossPanel::Reserve(std::uint32_t capacity) {
    const std::uint32_t stride = (capacity + 63) & ~std::uint32_t{63};
    Slab in = Allocate(nin_, stride);
    Slab out = Allocate(nout_, stride);
    for (std::size_t c = 0; c < nin_; ++c) std::copy_n(in_.get() + c * stride_, nrow_, in.get() + c * stride);
    for (std::size_t c = 0; c < nout_; ++c) std::copy_n(out_.get() + c * stride_, nrow_, out.get() + c * stride);
    in_ = std::move(in);
    out_ = std::move(out);
    valid_.Reserve(stride);
    changed_.Reserve(stride);
    stride_ = stride;
}

}  // namespace nq
