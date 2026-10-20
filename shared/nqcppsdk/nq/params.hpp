#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nq/abi.h>

namespace nq {

class Params {
public:
    explicit Params(const nq_params* p) noexcept : p_(p) {}

    std::string_view Symbol() const noexcept { return p_->symbol ? p_->symbol : ""; }
    std::int64_t TickSize() const noexcept { return p_->tick_size; }
    std::int64_t LotSize() const noexcept { return p_->lot_size; }
    std::int64_t PriceScale() const noexcept { return p_->price_scale; }
    std::int64_t QtyScale() const noexcept { return p_->qty_scale; }

    std::optional<std::string_view> Get(std::string_view key) const noexcept {
        for (std::uint32_t i = 0; i < p_->nkv; ++i) {
            if (p_->kv[i].key && key == p_->kv[i].key) return std::string_view(p_->kv[i].value ? p_->kv[i].value : "");
        }
        return std::nullopt;
    }

    std::string_view Require(std::string_view key) const {
        auto v = Get(key);
        if (!v) throw std::invalid_argument("missing param: " + std::string(key));
        return *v;
    }

    std::int64_t GetInt(std::string_view key, std::int64_t fallback) const {
        auto v = Get(key);
        return v ? ParseInt(*v, key) : fallback;
    }

    double GetDouble(std::string_view key, double fallback) const {
        auto v = Get(key);
        return v ? ParseDouble(*v, key) : fallback;
    }

    bool GetBool(std::string_view key, bool fallback) const {
        auto v = Get(key);
        if (!v) return fallback;
        if (*v == "1" || *v == "true" || *v == "yes") return true;
        if (*v == "0" || *v == "false" || *v == "no") return false;
        throw std::invalid_argument("bad bool param: " + std::string(key));
    }

    std::int64_t RequireInt(std::string_view key) const { return ParseInt(Require(key), key); }
    double RequireDouble(std::string_view key) const { return ParseDouble(Require(key), key); }

    const nq_params* Raw() const noexcept { return p_; }

private:
    static std::int64_t ParseInt(std::string_view v, std::string_view key) {
        std::string s(v);
        char* end = nullptr;
        const long long out = std::strtoll(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') throw std::invalid_argument("bad int param: " + std::string(key));
        return out;
    }

    static double ParseDouble(std::string_view v, std::string_view key) {
        std::string s(v);
        char* end = nullptr;
        const double out = std::strtod(s.c_str(), &end);
        if (s.empty() || *end != '\0') throw std::invalid_argument("bad double param: " + std::string(key));
        return out;
    }

    const nq_params* p_;
};

class ParamsBuilder {
public:
    ParamsBuilder& Symbol(std::string s) { symbol_ = std::move(s); return *this; }
    ParamsBuilder& TickSize(std::int64_t v) { tick_size_ = v; return *this; }
    ParamsBuilder& LotSize(std::int64_t v) { lot_size_ = v; return *this; }
    ParamsBuilder& PriceScale(std::int64_t v) { price_scale_ = v; return *this; }
    ParamsBuilder& QtyScale(std::int64_t v) { qty_scale_ = v; return *this; }

    ParamsBuilder& Set(std::string key, std::string value) {
        for (auto& [k, v] : kv_) {
            if (k == key) {
                v = std::move(value);
                return *this;
            }
        }
        kv_.emplace_back(std::move(key), std::move(value));
        return *this;
    }

    const nq_params* Build() {
        raw_.clear();
        raw_.reserve(kv_.size());
        for (const auto& [k, v] : kv_) raw_.push_back(nq_kv{k.c_str(), v.c_str()});
        params_ = nq_params{symbol_.c_str(), tick_size_, lot_size_, price_scale_, qty_scale_,
                            static_cast<std::uint32_t>(raw_.size()), raw_.data()};
        return &params_;
    }

private:
    std::string symbol_;
    std::int64_t tick_size_ = 1;
    std::int64_t lot_size_ = 1;
    std::int64_t price_scale_ = 1;
    std::int64_t qty_scale_ = 1;
    std::vector<std::pair<std::string, std::string>> kv_;
    std::vector<nq_kv> raw_;
    nq_params params_{};
};

}  // namespace nq
