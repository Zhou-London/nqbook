#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <vector>

namespace nq {

class Bitmap {
public:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    explicit Bitmap(std::uint32_t bits = 0) : words_(WordCount(bits), 0) {}

    void Resize(std::uint32_t bits) { words_.resize(WordCount(bits), 0); }

    bool Test(std::uint32_t i) const noexcept { return (words_[WordOf(i)] & MaskOf(i)) != 0; }
    void Set(std::uint32_t i) noexcept { words_[WordOf(i)] |= MaskOf(i); }
    void Clear(std::uint32_t i) noexcept { words_[WordOf(i)] &= ~MaskOf(i); }

    bool TestAndSet(std::uint32_t i) noexcept {
        std::uint64_t& word = words_[WordOf(i)];
        const bool was_set = (word & MaskOf(i)) != 0;
        word |= MaskOf(i);
        return was_set;
    }

    std::uint32_t FindSet(std::uint32_t first, std::uint32_t last) const noexcept {
        for (std::uint32_t w = WordOf(first); w <= WordOf(last); ++w) {
            std::uint64_t word = words_[w];
            if (w == WordOf(first)) word &= ~std::uint64_t{0} << (first % kWordBits);
            if (w == WordOf(last)) word &= ~std::uint64_t{0} >> (kWordBits - 1 - last % kWordBits);
            if (word != 0) return w * kWordBits + static_cast<std::uint32_t>(std::countr_zero(word));
        }
        return kNone;
    }

    const std::uint64_t* Words() const noexcept { return words_.data(); }

private:
    static constexpr std::uint32_t kWordBits = 64;

    static constexpr std::uint32_t WordCount(std::uint32_t bits) noexcept { return (bits + kWordBits - 1) / kWordBits; }
    static constexpr std::uint32_t WordOf(std::uint32_t i) noexcept { return i / kWordBits; }
    static constexpr std::uint64_t MaskOf(std::uint32_t i) noexcept { return std::uint64_t{1} << (i % kWordBits); }

    std::vector<std::uint64_t> words_;
};

}  // namespace nq
