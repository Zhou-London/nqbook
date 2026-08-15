<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Status: skeleton" src="https://img.shields.io/badge/status-skeleton-lightgrey" />
</p>

Project of NowQuant.

`nqbook` — the limit order book service. Namespace `nq`, C++23.

> **Status: skeleton.** The project configures, builds, and links, but nothing
> is implemented yet: `nq::Orderbook`'s handlers are empty inline stubs,
> `src/Orderbook.cpp` is an empty file, and `main()` returns immediately. What
> exists is the shape — the wire types and the three events the book reacts to.

## Shape

`include/common/common.h` defines the wire types, both fixed-layout and
trivially copyable so they can be memcpy'd off a feed:

| Type | Fields |
|---|---|
| `nq::Side` | `Buy` / `Sell`, a `uint8_t` enum |
| `nq::Order` | `symbol[16]`, `id`, `price`, `qty`, `event_time`, `side` |
| `nq::Trade` | same layout as `Order` |

Prices and quantities are unsigned integers, not floating point — scaling is the
feed's business, not the book's.

`nq::Orderbook` reacts to three events:

| Handler | Meaning |
|---|---|
| `OnOrder()` | an incremental order update |
| `OnTrade()` | an execution |
| `OnSnapshot()` | a full book replacement |

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nqbook
```

The intended build environment is the `cpp-dev` container from the
[Containers](../..) repository, which carries the toolchain and the
Arrow/ZeroMQ/spdlog stack this service will need.

## Related

- [nlib](https://github.com/Zhou-London/nlib) — the header-only data structures
  (`hive`, `map`, `single_queue`, `pool`, `memory_pool`) this book is meant to be
  built on.
