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

#include <nq/feature.hpp>

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
          plus_dm_(SmoothedAverage::Wilder(Period(p, "adx", 14))),
          minus_dm_(plus_dm_),
          adx_(plus_dm_),
          gain_(SmoothedAverage::Wilder(Period(p, "rsi", 6))),
          loss_(gain_),
          highest_(Period(p, "donchian", 20)),
          lowest_(Period(p, "donchian", 20)),
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
            AddDirectionalMove(high - high_, low_ - low);
            gain_.Add(std::max(close - close_, 0.0));
            loss_.Add(std::max(close_ - close, 0.0));
        }
        const double log_range = std::log(high / low);
        const double log_body = std::log(close / open);
        gk_ += 0.5 * log_range * log_range - kGkCloseWeight * log_body * log_body;

        range_atr_ = true_range / atr_.Value();
        atr_.Add(true_range);
        ema_.Add(close);
        highest_.Add(high);
        lowest_.Add(low);
        clv_ = Position(close, low, high);
        rvol_ = volume_.Add(hour, ohlcv.volume);
        high_ = high;
        low_ = low;
        close_ = close;
    }

    void Read(std::span<double> out) const noexcept {
        out[0] = ret_;
        out[1] = gap_;
        out[2] = rv_;
        out[3] = gk_;
        out[4] = atr_.Value() / close_;
        out[5] = (close_ - ema_.Value()) / atr_.Value();
        out[6] = adx_.Value();
        out[7] = Position(close_, lowest_.Value(), highest_.Value());
        out[8] = gain_.Ready() ? Share(gain_.Value(), gain_.Value() + loss_.Value(), 50.0) : kNaN;
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

    static double Share(double part, double total, double when_empty) noexcept {
        return total > 0.0 ? 100.0 * part / total : when_empty;
    }

    void AddDirectionalMove(double up, double down) noexcept {
        plus_dm_.Add(up > down && up > 0.0 ? up : 0.0);
        minus_dm_.Add(down > up && down > 0.0 ? down : 0.0);
        if (!plus_dm_.Ready()) return;
        const double plus = plus_dm_.Value();
        const double minus = minus_dm_.Value();
        adx_.Add(Share(std::fabs(plus - minus), plus + minus, 0.0));
    }

    double scale_;
    bool rth_only_;
    SmoothedAverage atr_;
    SmoothedAverage ema_;
    SmoothedAverage plus_dm_;
    SmoothedAverage minus_dm_;
    SmoothedAverage adx_;
    SmoothedAverage gain_;
    SmoothedAverage loss_;
    RollingExtreme<std::greater<double>> highest_;
    RollingExtreme<std::less<double>> lowest_;
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

}  // namespace nq_ta

NQ_OHLCV_PLUGIN(nq_ta::TechnicalAnalysis)
