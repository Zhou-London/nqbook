#include <cstdint>
#include <span>

#include <nq/feature.hpp>

namespace nqtest {

struct Volume : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"vol", "qty", "cumulative traded volume", nq::kCumulative},
    });

    explicit Volume(const nq::Params&) {}

    void OnEvent(const nq::Event& e, const nq::BookView&) noexcept {
        if (e.action == nq::Action::Trade) vol_ += e.filled;
    }

    void Read(std::span<double> out, const nq::BookView&) const noexcept { out[0] = static_cast<double>(vol_); }

private:
    std::int64_t vol_ = 0;
};

}  // namespace nqtest

NQ_PLUGIN(nqtest::Volume)
