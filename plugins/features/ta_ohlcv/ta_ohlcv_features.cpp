#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nq/cross.hpp>

namespace nq_ta {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline int EasternHour(std::int64_t ts) noexcept {
    using namespace std::chrono;
    const sys_seconds utc{seconds{ts / 1'000'000'000}};
    const year y = year_month_day{floor<days>(utc)}.year();
    const sys_seconds dst_start = sys_days{y / March / Sunday[2]} + hours{7};
    const sys_seconds dst_end = sys_days{y / November / Sunday[1]} + hours{6};
    const sys_seconds eastern = utc - hours{utc >= dst_start && utc < dst_end ? 4 : 5};
    return static_cast<int>(duration_cast<hours>(eastern - floor<days>(eastern)).count());
}

inline double Position(double x, double low, double high) noexcept {
    if (high > low) return (x - low) / (high - low);
    return high == low ? 0.5 : kNaN;
}

inline double Share(double part, double total, double when_empty) noexcept {
    return total > 0.0 ? 100.0 * part / total : when_empty;
}

class SmoothedAverage {
public:
    static SmoothedAverage Wilder(std::int64_t period) { return {period, 1.0 / static_cast<double>(period)}; }
    static SmoothedAverage Exponential(std::int64_t period) { return {period, 2.0 / static_cast<double>(period + 1)}; }

    void Add(double x) noexcept {
        if (count_ < period_) {
            sum_ += x;
            if (++count_ == period_) value_ = sum_ / static_cast<double>(period_);
        } else {
            value_ += alpha_ * (x - value_);
        }
    }

    bool Ready() const noexcept { return count_ == period_; }
    double Value() const noexcept { return value_; }

private:
    SmoothedAverage(std::int64_t period, double alpha) : period_(period), alpha_(alpha) {}

    std::int64_t period_;
    double alpha_;
    std::int64_t count_ = 0;
    double sum_ = 0.0;
    double value_ = kNaN;
};

template <class Better>
class RollingExtreme {
public:
    explicit RollingExtreme(std::int64_t period) : period_(period) {}

    void Add(double x) {
        while (!window_.empty() && !Better{}(window_.back().value, x)) window_.pop_back();
        window_.push_back({count_, x});
        if (window_.front().index <= count_ - period_) window_.pop_front();
        ++count_;
    }

    double Value() const noexcept { return count_ >= period_ ? window_.front().value : kNaN; }

private:
    struct Entry {
        std::int64_t index;
        double value;
    };

    std::int64_t period_;
    std::int64_t count_ = 0;
    std::deque<Entry> window_;
};

class DirectionalIndex {
public:
    explicit DirectionalIndex(std::int64_t period) : plus_(SmoothedAverage::Wilder(period)), minus_(plus_), adx_(plus_) {}

    void Add(double up, double down) noexcept {
        plus_.Add(up > down && up > 0.0 ? up : 0.0);
        minus_.Add(down > up && down > 0.0 ? down : 0.0);
        if (!plus_.Ready()) return;
        const double plus = plus_.Value();
        const double minus = minus_.Value();
        adx_.Add(Share(std::fabs(plus - minus), plus + minus, 0.0));
    }

    double Value() const noexcept { return adx_.Value(); }

private:
    SmoothedAverage plus_;
    SmoothedAverage minus_;
    SmoothedAverage adx_;
};

class RelativeStrength {
public:
    explicit RelativeStrength(std::int64_t period) : gain_(SmoothedAverage::Wilder(period)), loss_(gain_) {}

    void Add(double change) noexcept {
        gain_.Add(std::max(change, 0.0));
        loss_.Add(std::max(-change, 0.0));
    }

    double Value() const noexcept {
        return gain_.Ready() ? Share(gain_.Value(), gain_.Value() + loss_.Value(), 50.0) : kNaN;
    }

private:
    SmoothedAverage gain_;
    SmoothedAverage loss_;
};

class DonchianChannel {
public:
    explicit DonchianChannel(std::int64_t period) : highest_(period), lowest_(period) {}

    void Add(double high, double low) {
        highest_.Add(high);
        lowest_.Add(low);
    }

    double PositionOf(double close) const noexcept { return Position(close, lowest_.Value(), highest_.Value()); }

private:
    RollingExtreme<std::greater<double>> highest_;
    RollingExtreme<std::less<double>> lowest_;
};

class RelativeVolume {
public:
    explicit RelativeVolume(std::int64_t days) : days_(days), history_(static_cast<std::size_t>(kHoursPerDay * days), 0.0) {}

    double Add(int hour, double volume) noexcept {
        Slot& slot = slots_[static_cast<std::size_t>(hour)];
        const double ratio = slot.count >= days_ ? volume / (slot.sum / static_cast<double>(days_)) : kNaN;
        double& oldest = history_[static_cast<std::size_t>(hour * days_ + slot.count % days_)];
        slot.sum += volume - oldest;
        oldest = volume;
        ++slot.count;
        return ratio;
    }

private:
    static constexpr std::int64_t kHoursPerDay = 24;

    struct Slot {
        std::int64_t count = 0;
        double sum = 0.0;
    };

    std::int64_t days_;
    std::vector<double> history_;
    std::array<Slot, kHoursPerDay> slots_{};
};

class TechnicalAnalysis : public nq::FeatureBase {
public:
    static constexpr auto kColumns = nq::MakeColumns({
        {"ret", "log", "close-to-close log return", nq::kCumulative},
        {"gap", "log", "log return from the previous close to the open", nq::kCumulative},
        {"rv", "var", "squared close-to-close log return", nq::kCumulative},
        {"gk", "var", "Garman-Klass variance", nq::kCumulative},
        {"natr", "ratio", "average true range over close"},
        {"ema_dev", "atr", "close minus its EMA, in average true ranges"},
        {"adx", "index", "average directional index, 0 to 100"},
        {"donchian_pos", "ratio", "position of close between the rolling low and the rolling high, 0 to 1"},
        {"rsi", "index", "relative strength index, 0 to 100"},
        {"clv", "ratio", "position of close between low and high of the last record, 0 to 1"},
        {"range_atr", "atr", "true range of the last record over the average true range before it"},
        {"rvol", "ratio", "volume over the mean volume of the same Eastern hour on earlier days"},
    });

    static constexpr int kRthFirstHour = 9;
    static constexpr int kRthLastHour = 15;
    static constexpr double kGkCloseWeight = 2.0 * std::numbers::ln2 - 1.0;

    explicit TechnicalAnalysis(const nq::Params& p)
        : scale_(static_cast<double>(p.PriceScale())),
          rth_only_(p.GetBool("rth_only", false)),
          atr_(SmoothedAverage::Wilder(Period(p, "atr", 14))),
          ema_(SmoothedAverage::Exponential(Period(p, "ema", 20))),
          adx_(Period(p, "adx", 14)),
          rsi_(Period(p, "rsi", 6)),
          donchian_(Period(p, "donchian", 20)),
          volume_(Period(p, "rvol_days", 20)) {}

    void OnOhlcv(const nq::Ohlcv& ohlcv) noexcept {
        const int hour = EasternHour(ohlcv.ts);
        if (rth_only_ && (hour < kRthFirstHour || hour > kRthLastHour)) return;

        const double open = static_cast<double>(ohlcv.open) / scale_;
        const double high = static_cast<double>(ohlcv.high) / scale_;
        const double low = static_cast<double>(ohlcv.low) / scale_;
        const double close = static_cast<double>(ohlcv.close) / scale_;

        double true_range = high - low;
        if (!std::isnan(close_)) {
            const double ret = std::log(close / close_);
            ret_ += ret;
            gap_ += std::log(open / close_);
            rv_ += ret * ret;
            true_range = std::max(high, close_) - std::min(low, close_);
            adx_.Add(high - high_, low_ - low);
            rsi_.Add(close - close_);
        }
        const double log_range = std::log(high / low);
        const double log_body = std::log(close / open);
        gk_ += 0.5 * log_range * log_range - kGkCloseWeight * log_body * log_body;

        range_atr_ = true_range / atr_.Value();
        atr_.Add(true_range);
        ema_.Add(close);
        donchian_.Add(high, low);
        clv_ = Position(close, low, high);
        rvol_ = volume_.Add(hour, ohlcv.volume);
        high_ = high;
        low_ = low;
        close_ = close;
    }

    void OnWarmup(const nq::Ohlcv& ohlcv) noexcept { OnOhlcv(ohlcv); }

    void Read(std::span<double> out) const noexcept {
        out[0] = ret_;
        out[1] = gap_;
        out[2] = rv_;
        out[3] = gk_;
        out[4] = atr_.Value() / close_;
        out[5] = (close_ - ema_.Value()) / atr_.Value();
        out[6] = adx_.Value();
        out[7] = donchian_.PositionOf(close_);
        out[8] = rsi_.Value();
        out[9] = clv_;
        out[10] = range_atr_;
        out[11] = rvol_;
    }

private:
    static std::int64_t Period(const nq::Params& p, const char* key, std::int64_t fallback) {
        const std::int64_t period = p.GetInt(key, fallback);
        if (period < 1) throw std::invalid_argument(std::string(key) + " must be at least 1");
        return period;
    }

    double scale_;
    bool rth_only_;
    SmoothedAverage atr_;
    SmoothedAverage ema_;
    DirectionalIndex adx_;
    RelativeStrength rsi_;
    DonchianChannel donchian_;
    RelativeVolume volume_;
    double high_ = kNaN;
    double low_ = kNaN;
    double close_ = kNaN;
    double ret_ = 0.0;
    double gap_ = 0.0;
    double rv_ = 0.0;
    double gk_ = 0.0;
    double clv_ = kNaN;
    double range_atr_ = kNaN;
    double rvol_ = kNaN;
};

class TechnicalCross {
public:
    static constexpr auto kInputs =
        nq::MakeInputs({"natr", "ema_dev", "adx", "donchian_pos", "rsi", "clv", "range_atr", "rvol"});
    static constexpr auto kColumns = nq::MakeColumns({
        {"natr_rank", "rank", "rank of natr across the instruments at the boundary, 0 to 1"},
        {"natr_z", "sd", "z-score of natr across the instruments at the boundary, 0 when all are equal"},
        {"ema_dev_rank", "rank", "rank of ema_dev across the instruments at the boundary, 0 to 1"},
        {"ema_dev_z", "sd", "z-score of ema_dev across the instruments at the boundary, 0 when all are equal"},
        {"adx_rank", "rank", "rank of adx across the instruments at the boundary, 0 to 1"},
        {"adx_z", "sd", "z-score of adx across the instruments at the boundary, 0 when all are equal"},
        {"donchian_pos_rank", "rank", "rank of donchian_pos across the instruments at the boundary, 0 to 1"},
        {"donchian_pos_z", "sd", "z-score of donchian_pos across the instruments at the boundary, 0 when all are equal"},
        {"rsi_rank", "rank", "rank of rsi across the instruments at the boundary, 0 to 1"},
        {"rsi_z", "sd", "z-score of rsi across the instruments at the boundary, 0 when all are equal"},
        {"clv_rank", "rank", "rank of clv across the instruments at the boundary, 0 to 1"},
        {"clv_z", "sd", "z-score of clv across the instruments at the boundary, 0 when all are equal"},
        {"range_atr_rank", "rank", "rank of range_atr across the instruments at the boundary, 0 to 1"},
        {"range_atr_z", "sd", "z-score of range_atr across the instruments at the boundary, 0 when all are equal"},
        {"rvol_rank", "rank", "rank of rvol across the instruments at the boundary, 0 to 1"},
        {"rvol_z", "sd", "z-score of rvol across the instruments at the boundary, 0 when all are equal"},
    });
    static constexpr std::size_t kColumnsPerInput = 2;
    static_assert(kColumns.size() == kColumnsPerInput * kInputs.size());

    explicit TechnicalCross(const nq::Params& p) : min_members_(p.GetInt("cross_min", 20)) {
        if (min_members_ < 2) throw std::invalid_argument("cross_min must be at least 2");
    }

    void Apply(const nq::CrossView& view) noexcept {
        const std::size_t nrow = view.RowCount();
        for (std::size_t c = 0; c < kColumns.size(); ++c) std::fill_n(view.Out(c).begin(), nrow, kNaN);

        for (std::size_t s = 0; s < kInputs.size(); ++s) {
            entries_.clear();
            const std::span<const double> in = view.In(s);
            for (const std::uint32_t row : view.Changed()) {
                if (std::isfinite(in[row])) entries_.push_back({in[row], row});
            }
            if (static_cast<std::int64_t>(entries_.size()) >= min_members_) {
                Write(view.Out(RankColumn(s)), view.Out(ZColumn(s)));
            }
        }
    }

private:
    struct Entry {
        double value;
        std::uint32_t row;
    };

    static constexpr std::size_t RankColumn(std::size_t input) noexcept { return kColumnsPerInput * input; }
    static constexpr std::size_t ZColumn(std::size_t input) noexcept { return kColumnsPerInput * input + 1; }

    void Write(std::span<double> rank, std::span<double> z) noexcept {
        const std::size_t n = entries_.size();
        double sum = 0.0;
        for (const Entry& e : entries_) sum += e.value;
        const double mean = sum / static_cast<double>(n);
        double square = 0.0;
        for (const Entry& e : entries_) square += (e.value - mean) * (e.value - mean);
        const double sd = std::sqrt(square / static_cast<double>(n));
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.value < b.value; });
        // Rounding in the mean can leave sd a few ulps above 0 when all values are equal.
        const bool equal = entries_.front().value == entries_.back().value;
        for (const Entry& e : entries_) z[e.row] = equal ? 0.0 : (e.value - mean) / sd;

        for (std::size_t first = 0; first < n;) {
            std::size_t last = first;
            while (last + 1 < n && entries_[last + 1].value == entries_[first].value) ++last;
            const double scaled = 0.5 * static_cast<double>(first + last) / static_cast<double>(n - 1);
            for (std::size_t i = first; i <= last; ++i) rank[entries_[i].row] = scaled;
            first = last + 1;
        }
    }

    std::int64_t min_members_;
    std::vector<Entry> entries_;
};

class CrossReadback : public nq::FeatureBase {
public:
    static constexpr auto kColumns = TechnicalCross::kColumns;

    explicit CrossReadback(const nq::Params&) {}

    void BindCross(nq::CrossRow row) noexcept { cross_ = row; }

    void Read(std::span<double> out) const noexcept {
        for (std::size_t c = 0; c < kColumns.size(); ++c) out[c] = cross_ ? cross_[c] : kNaN;
    }

private:
    nq::CrossRow cross_;
};

}  // namespace nq_ta

NQ_OHLCV_PLUGIN(nq_ta::TechnicalAnalysis, nq_ta::CrossReadback)
NQ_CROSS(nq_ta::TechnicalCross)
