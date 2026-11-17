#include <limits>
#include <span>

#include <nq/cross.hpp>
#include <nq/cross_stats.hpp>

namespace nq_example {

struct RelativeMid : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"mid", "price", "midpoint of best bid and ask"},
        {"market_mid", "price", "cross-sectional mean of mid at the last boundary"},
        {"rel_mid", "price", "mid minus market_mid"},
    });
    static constexpr nq::Requires kRequires = nq::Requires::L1;

    explicit RelativeMid(const nq::Params& p) : scale_(static_cast<double>(p.PriceScale())) {}

    void BindCross(nq::CrossRow row) noexcept { cross_ = row; }

    void Read(std::span<double> out, const nq::BookView& book) const noexcept {
        const auto bid = book.Best(nq::Side::Bid);
        const auto ask = book.Best(nq::Side::Ask);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        out[0] = bid && ask ? static_cast<double>(ask->price + bid->price) / (2.0 * scale_) : nan;
        out[1] = cross_ ? cross_[0] : nan;
        out[2] = out[0] - out[1];
    }

private:
    double scale_;
    nq::CrossRow cross_;
};

struct MidCross {
    static constexpr auto kInputs = nq::MakeInputs({"mid"});
    static constexpr auto kColumns = nq::MakeColumns({
        {"mean", "price", "cross-sectional mean of mid"},
        {"z", "", "cross-sectional z-score of mid"},
        {"rank", "", "cross-sectional rank of mid in [0, 1]"},
    });

    explicit MidCross(const nq::Params&) {}

    void Apply(const nq::CrossView& view) noexcept {
        const auto mid = view.In(0);
        moments_.Update(mid, view.Changed());
        rank_.Update(mid, view.Changed());

        const double mean = moments_.Mean();
        const double sd = moments_.Std();
        const auto mean_out = view.Out(0);
        const auto z = view.Out(1);
        for (std::size_t r = 0; r < mid.size(); ++r) {
            mean_out[r] = mean;
            z[r] = (mid[r] - mean) / sd;
        }
        rank_.Write(view.Out(2));
    }

private:
    nq::RunningMoments moments_;
    nq::RunningRank rank_;
};

}  // namespace nq_example

NQ_PLUGIN(nq_example::RelativeMid)
NQ_CROSS(nq_example::MidCross)
