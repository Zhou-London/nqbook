#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

#include <nq/adapter.hpp>

#include "mbo_file.hpp"

namespace nq_replay {

class Replay : public nq::ThreadedAdapter<Replay> {
public:
    static constexpr const char* kName = "replay";
    static constexpr nq::Provides kProvides = nq::Provides::MBO;

    Replay(const nq::Params& p, nq::Sink sink) : sink_(sink), reader_(std::string(p.Require("path"))) {
        for (const auto& r : reader_.Instruments()) {
            table_.push_back(nq::Instrument{r.id, r.symbol, r.tick_size, r.lot_size});
        }
    }

    void Run(std::stop_token st) {
        nq_mbo m;
        for (;;) {
            if (st.stop_requested()) return;
            const int rc = reader_.Next(m);
            if (rc == 1) {
                sink_.Push(m);
                continue;
            }
            sink_.End(rc == 0 ? 0 : 2);
            return;
        }
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) {
            if (i.id == id) return &i;
        }
        return nullptr;
    }

private:
    nq::Sink sink_;
    nq::mbo_file::Reader reader_;
    std::vector<nq::Instrument> table_;
};

}  // namespace nq_replay

NQ_ADAPTER(nq_replay::Replay)
