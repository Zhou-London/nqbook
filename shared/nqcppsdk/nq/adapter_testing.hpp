#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

#include <nq/adapter.hpp>

namespace nq::testing {

template <class A>
    requires AdapterType<A> || OhlcvAdapterType<A>
class AdapterSandbox {
public:
    using Record = std::conditional_t<AdapterType<A>, Mbo, Ohlcv>;

    explicit AdapterSandbox(const nq_params* params) : adapter_(Params(params)) {}

    A& Adapter() noexcept { return adapter_; }
    const A& Adapter() const noexcept { return adapter_; }

    Next Step(Record& out) {
        last_ = adapter_.Next(out);
        return last_;
    }

    std::vector<Record> Drain(std::size_t max = std::numeric_limits<std::size_t>::max()) {
        std::vector<Record> out;
        Record record{};
        while (out.size() < max) {
            const Next r = Step(record);
            if (r == Next::Event) {
                out.push_back(record);
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
