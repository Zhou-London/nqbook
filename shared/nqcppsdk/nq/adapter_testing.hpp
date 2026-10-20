#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include <nq/adapter.hpp>

namespace nq::testing {

template <AdapterType A>
class AdapterSandbox {
public:
    explicit AdapterSandbox(const nq_params* params) : adapter_(Params(params)) {}

    A& Adapter() noexcept { return adapter_; }
    const A& Adapter() const noexcept { return adapter_; }

    Next Step(Mbo& out) {
        last_ = adapter_.Next(out);
        return last_;
    }

    std::vector<Mbo> Drain(std::size_t max = std::numeric_limits<std::size_t>::max()) {
        std::vector<Mbo> out;
        Mbo m;
        while (out.size() < max) {
            const Next r = Step(m);
            if (r == Next::Event) {
                out.push_back(m);
                continue;
            }
            if (r == Next::Again) continue;
            break;
        }
        return out;
    }

    Next LastStatus() const noexcept { return last_; }

private:
    A adapter_;
    Next last_ = Next::Again;
};

}  // namespace nq::testing
