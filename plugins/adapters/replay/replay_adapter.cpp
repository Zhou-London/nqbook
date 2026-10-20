#include <cstdint>
#include <string>
#include <vector>

#include <nq/adapter.hpp>

#include <nq/mbo_file.hpp>

namespace nq_replay {

class Replay : public nq::AdapterBase {
public:
    static constexpr const char* kName = "replay";
    static constexpr nq::Provides kProvides = nq::Provides::MBO;

    explicit Replay(const nq::Params& p) : reader_(std::string(p.Require("path"))) {
        for (const auto& r : reader_.Instruments()) {
            table_.push_back(nq::Instrument{r.id, r.symbol, r.tick_size, r.lot_size});
        }
    }

    nq::Next Next(nq::Mbo& out) {
        if (done_) return done_status_;
        nq_mbo raw;
        const int rc = reader_.Next(raw);
        if (rc == 1) {
            out = nq::Mbo(raw);
            return nq::Next::Event;
        }
        done_ = true;
        done_status_ = rc == 0 ? nq::Next::End : nq::Next::Error;
        return done_status_;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    nq::mbo_file::Reader reader_;
    std::vector<nq::Instrument> table_;
    bool done_ = false;
    nq::Next done_status_ = nq::Next::End;
};

}  // namespace nq_replay

NQ_ADAPTER(nq_replay::Replay)
