#include <cstdint>

#include <nq/adapter.hpp>

namespace nqtest {

class FlakyWarmup : public nq::AdapterBase {
public:
    static constexpr const char* kName = "flaky_warmup";

    explicit FlakyWarmup(const nq::Params& p) : stalls_(p.GetBool("stall", false)) {}

    nq::Next Warmup(nq::Ohlcv& out) {
        if (stalls_) return nq::Next::Again;
        switch (calls_++) {
            case 0: return nq::Next::Again;
            case 1:
                out = nq::Ohlcv{-1, 100, 102, 99, 101, 1.0, 1};
                return nq::Next::Event;
            default: return nq::Next::Error;
        }
    }

    nq::Next Next(nq::Mbo&) { return nq::Next::End; }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        return id == 1 ? &instrument_ : nullptr;
    }

private:
    bool stalls_;
    int calls_ = 0;
    nq::Instrument instrument_{1, "FLAKY", 10, 1};
};

}  // namespace nqtest

NQ_ADAPTER(nqtest::FlakyWarmup)
