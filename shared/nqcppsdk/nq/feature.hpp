#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>

#include <nq/abi.h>
#include <nq/book_view.hpp>
#include <nq/event.hpp>
#include <nq/params.hpp>

#ifndef NQ_ABI_HASH
#error "NQ_ABI_HASH is not defined: link the target against nqcppsdk::nqcppsdk"
#endif

namespace nq {

using Column = nq_column;

inline constexpr std::uint8_t kSnapshot = NQ_COLUMN_SNAPSHOT;
inline constexpr std::uint8_t kCumulative = NQ_COLUMN_CUMULATIVE;

enum class Requires : std::uint8_t {
    L1 = NQ_REQUIRES_L1,
    MBP = NQ_REQUIRES_MBP,
    MBO = NQ_REQUIRES_MBO
};

struct ColumnDef {
    const char* name;
    const char* unit = "";
    const char* doc = "";
    std::uint8_t kind = kSnapshot;
};

template <std::size_t N>
constexpr std::array<Column, N> MakeColumns(const ColumnDef (&cols)[N]) {
    std::array<Column, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = Column{cols[i].name, cols[i].unit, cols[i].doc, cols[i].kind};
    return out;
}

struct FeatureBase {
    void OnEvent(const Event&, const BookView&) noexcept {}
    void OnControl(const Control&) noexcept {}
};

template <class F>
concept FeatureType =
    std::is_constructible_v<F, const Params&> &&
    requires(F& f, const F& cf, const Event& e, const BookView& b, const Control& c,
             std::span<double> out) {
        { F::kColumns.size() } -> std::convertible_to<std::size_t>;
        { f.OnEvent(e, b) } noexcept;
        { cf.Read(out, b) } noexcept;
        { f.OnControl(c) } noexcept;
    };

namespace detail {

template <class F>
constexpr std::uint8_t RequiresOf() noexcept {
    if constexpr (requires { F::kRequires; }) return static_cast<std::uint8_t>(F::kRequires);
    else return NQ_REQUIRES_MBO;
}

template <std::size_t... Ns>
constexpr std::array<Column, (Ns + ... + 0)> Concat(const std::array<Column, Ns>&... arrays) {
    std::array<Column, (Ns + ... + 0)> out{};
    std::size_t i = 0;
    auto append = [&](const auto& a) {
        for (const auto& c : a) out[i++] = c;
    };
    (append(arrays), ...);
    return out;
}

template <class>
const Params& Same(const Params& p) noexcept { return p; }

}  // namespace detail

template <FeatureType... Fs>
class Bundle {
    static_assert(sizeof...(Fs) > 0);

public:
    static constexpr std::size_t kNcol = (Fs::kColumns.size() + ... + 0);
    static constexpr std::array<Column, kNcol> kColumns = detail::Concat(Fs::kColumns...);
    static constexpr std::uint8_t kRequires = std::max({detail::RequiresOf<Fs>()...});

    static const nq_schema* Schema() noexcept {
        static const nq_schema schema{NQ_ABI_HASH, NQ_SDK_VERSION, kRequires,
                                      static_cast<std::uint32_t>(kNcol), kColumns.data()};
        return &schema;
    }

    explicit Bundle(const Params& p) : features_(detail::Same<Fs>(p)...) {}

    void OnEvent(const Event& e, const BookView& b) noexcept {
        std::apply([&](auto&... f) { (f.OnEvent(e, b), ...); }, features_);
    }

    void Read(std::span<double> out, const BookView& b) const noexcept {
        std::size_t off = 0;
        std::apply(
            [&](const auto&... f) {
                ((f.Read(out.subspan(off, ColsOf(f)), b), off += ColsOf(f)), ...);
            },
            features_);
    }

    void OnControl(const Control& c) noexcept {
        std::apply([&](auto&... f) { (f.OnControl(c), ...); }, features_);
    }

    template <std::size_t I>
    auto& Get() noexcept { return std::get<I>(features_); }

    template <std::size_t I>
    const auto& Get() const noexcept { return std::get<I>(features_); }

private:
    template <class F>
    static constexpr std::size_t ColsOf(const F&) noexcept { return F::kColumns.size(); }

    std::tuple<Fs...> features_;
};

}  // namespace nq

#define NQ_PLUGIN(...)                                                                       \
    using nq_plugin_bundle_t = ::nq::Bundle<__VA_ARGS__>;                                    \
    extern "C" {                                                                             \
    NQ_EXPORT const nq_schema* nq_describe(void) { return nq_plugin_bundle_t::Schema(); }   \
    NQ_EXPORT void* nq_init(const nq_params* p) {                                            \
        try {                                                                                \
            return new nq_plugin_bundle_t(::nq::Params(p));                                  \
        } catch (...) {                                                                      \
            return nullptr;                                                                  \
        }                                                                                    \
    }                                                                                        \
    NQ_EXPORT void nq_destroy(void* s) { delete static_cast<nq_plugin_bundle_t*>(s); }       \
    NQ_EXPORT void nq_on_event(void* s, const nq_event* e, const nq_book_view* b) {          \
        static_cast<nq_plugin_bundle_t*>(s)->OnEvent(::nq::Event(*e), ::nq::BookView(b));    \
    }                                                                                        \
    NQ_EXPORT void nq_read(void* s, const nq_book_view* b, double* out) {                    \
        static_cast<nq_plugin_bundle_t*>(s)->Read(                                           \
            std::span<double>(out, nq_plugin_bundle_t::kNcol), ::nq::BookView(b));           \
    }                                                                                        \
    NQ_EXPORT void nq_on_control(void* s, const nq_control* c) {                             \
        static_cast<nq_plugin_bundle_t*>(s)->OnControl(::nq::Control(*c));                   \
    }                                                                                        \
    }
