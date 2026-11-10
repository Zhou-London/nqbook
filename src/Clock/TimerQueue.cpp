#include <Clock/TimerQueue.h>

#include <utility>

namespace nq {

void TimerQueue::WakeBy(std::uint32_t key, std::int64_t due) {
    if (key >= due_.size()) due_.resize(key + 1, kNever);
    if (due >= due_[key]) return;
    
    due_[key] = due;
    std::vector<std::uint64_t>& bits = BucketFor(due);
    
    const std::size_t w = key / 64;
    if (w >= bits.size()) bits.resize(w + 1, 0);
    
    bits[w] |= std::uint64_t{1} << (key % 64);
}

std::vector<std::uint64_t>& TimerQueue::BucketFor(std::int64_t due) {
    if (auto it = buckets_.find(due); it != buckets_.end()) return it->second;
    if (spare_.empty()) return buckets_.try_emplace(due).first->second;
    
    Buckets::node_type node = std::move(spare_.back());
    spare_.pop_back();
    node.key() = due;
    return buckets_.insert(std::move(node)).position->second;
}

}  // namespace nq
