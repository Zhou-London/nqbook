#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace nq {

class TimerQueue {
public:
    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

    void WakeBy(std::uint32_t key, std::int64_t due);

    template <typename Wake>
    void AdvanceTo(std::int64_t now, Wake&& wake) {
        while (!buckets_.empty() && buckets_.begin()->first <= now) {
            auto node = buckets_.extract(buckets_.begin());
            const std::int64_t due = node.key();
            std::vector<std::uint64_t>& bits = node.mapped();
            
            for (std::size_t w = 0; w < bits.size(); ++w) {
                std::uint64_t word = std::exchange(bits[w], 0);
                
                for (; word != 0; word &= word - 1) {
                    const auto key = static_cast<std::uint32_t>(w * 64 + std::countr_zero(word));
                    if (due_[key] != due) continue;
                    due_[key] = kNever;
                    wake(key);
                }
            }
            
            spare_.push_back(std::move(node));
        }
    }

    std::int64_t Due(std::uint32_t key) const noexcept { return key < due_.size() ? due_[key] : kNever; }

private:
    using Buckets = std::map<std::int64_t, std::vector<std::uint64_t>>;

    std::vector<std::uint64_t>& BucketFor(std::int64_t due);

    Buckets buckets_;
    std::vector<Buckets::node_type> spare_;
    std::vector<std::int64_t> due_;
};

}  // namespace nq
