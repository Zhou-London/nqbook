#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include <nq/adapter_abi.h>
#include <nq/types.hpp>
#include <nq/instrument.hpp>
#include <nq/params.hpp>

#ifndef NQ_ADAPTER_ABI_HASH
#error "NQ_ADAPTER_ABI_HASH is not defined: link the target against nqcppsdk::nqcppsdk"
#endif

namespace nq {

enum class Provides : std::uint8_t {
    None = NQ_REQUIRES_NONE,
    L1 = NQ_REQUIRES_L1,
    MBP = NQ_REQUIRES_MBP,
    MBO = NQ_REQUIRES_MBO
};

enum class Next : int {
    Event = NQ_NEXT_EVENT,
    Again = NQ_NEXT_AGAIN,
    End = NQ_NEXT_END,
    Error = NQ_NEXT_ERROR
};

struct Mbo {
    std::int64_t ts = 0;
    std::uint64_t id = 0;
    std::int64_t price = 0;
    std::int64_t qty = 0;
    std::uint32_t instrument = 0;
    Side side = Side::Bid;
    Action action = Action::Add;

    constexpr Mbo() = default;
    constexpr explicit Mbo(const nq_mbo& m) noexcept
        : ts(m.ts),
          id(m.id),
          price(m.price),
          qty(m.qty),
          instrument(m.instrument),
          side(static_cast<Side>(m.side)),
          action(static_cast<Action>(m.action)) {}

    constexpr nq_mbo Raw() const noexcept {
        return nq_mbo{ts, id, price, qty, instrument,
                      static_cast<std::uint8_t>(side), static_cast<std::uint8_t>(action)};
    }
};

struct AdapterBase {
    const Instrument* FindInstrument(std::uint32_t) const noexcept { return nullptr; }
};

namespace detail {

template <class A>
concept AdapterCore =
    std::is_constructible_v<A, const Params&> &&
    requires(const A& ca, std::uint32_t id) {
        { A::kName } -> std::convertible_to<const char*>;
        { ca.FindInstrument(id) } noexcept -> std::convertible_to<const Instrument*>;
    };

}  // namespace detail

template <class A>
concept AdapterType = detail::AdapterCore<A> && requires(A& a, Mbo& out) {
    { a.Next(out) } -> std::same_as<Next>;
};

template <class A>
concept OhlcvAdapterType = detail::AdapterCore<A> && requires(A& a, Ohlcv& out) {
    { a.Next(out) } -> std::same_as<Next>;
};

namespace detail {

template <class A>
constexpr std::uint8_t ProvidesOf() noexcept {
    if constexpr (requires { A::kProvides; }) return static_cast<std::uint8_t>(A::kProvides);
    else if constexpr (OhlcvAdapterType<A>) return NQ_REQUIRES_NONE;
    else return NQ_REQUIRES_MBO;
}

template <class A>
int NextRaw(A& adapter, nq_mbo& out) {
    Mbo m;
    const Next r = adapter.Next(m);
    if (r == Next::Event) out = m.Raw();
    return static_cast<int>(r);
}

template <class A>
int NextRaw(A& adapter, nq_ohlcv& out) {
    return static_cast<int>(adapter.Next(out));
}

}  // namespace detail

}  // namespace nq

#define NQ_DETAIL_ADAPTER(Type, feed, next_symbol, raw_record)                                  \
    extern "C" {                                                                                \
    NQ_EXPORT const nq_adapter_schema* nq_adapter_describe(void) {                              \
        static const nq_adapter_schema schema{NQ_ADAPTER_ABI_HASH, NQ_SDK_VERSION, Type::kName, \
                                              ::nq::detail::ProvidesOf<Type>(), feed};          \
        return &schema;                                                                         \
    }                                                                                           \
    NQ_EXPORT void* nq_adapter_init(const nq_params* p) {                                       \
        try {                                                                                   \
            return new Type(::nq::Params(p));                                                   \
        } catch (...) {                                                                         \
            return nullptr;                                                                     \
        }                                                                                       \
    }                                                                                           \
    NQ_EXPORT int next_symbol(void* s, raw_record* out) {                                       \
        try {                                                                                   \
            return ::nq::detail::NextRaw(*static_cast<Type*>(s), *out);                         \
        } catch (...) {                                                                         \
            return NQ_NEXT_ERROR;                                                               \
        }                                                                                       \
    }                                                                                           \
    NQ_EXPORT int nq_adapter_instrument(void* s, uint32_t id, nq_instrument* out) {             \
        const ::nq::Instrument* i = static_cast<const Type*>(s)->FindInstrument(id);            \
        if (!i) return 1;                                                                       \
        *out = nq_instrument{i->id, i->symbol.c_str(), i->tick_size, i->lot_size};              \
        return 0;                                                                               \
    }                                                                                           \
    NQ_EXPORT void nq_adapter_destroy(void* s) { delete static_cast<Type*>(s); }                \
    }

#define NQ_ADAPTER(Type)                                                              \
    static_assert(::nq::AdapterType<Type> && !::nq::OhlcvAdapterType<Type>,           \
                  "NQ_ADAPTER needs Next(nq::Mbo&) as the only Next");                \
    NQ_DETAIL_ADAPTER(Type, NQ_FEED_MBO, nq_adapter_next, nq_mbo)

#define NQ_OHLCV_ADAPTER(Type)                                                        \
    static_assert(::nq::OhlcvAdapterType<Type> && !::nq::AdapterType<Type>,           \
                  "NQ_OHLCV_ADAPTER needs Next(nq::Ohlcv&) as the only Next");        \
    NQ_DETAIL_ADAPTER(Type, NQ_FEED_OHLCV, nq_adapter_next_ohlcv, nq_ohlcv)
