<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Depends on nlib" src="https://img.shields.io/badge/submodule-nlib-4c1?logo=git&logoColor=white" />
</p>

Project of NowQuant.

`nqbook` — the limit order book service. Namespace `nq`, C++23.

A price-time-priority book for one instrument, rebuilt by replaying an
order-by-order feed. It does not match: crossing orders rest until the feed
reports their trades.

## Shape

Wire types come from the [nlib](https://github.com/Zhou-London/nlib) submodule
(`nlib::order`, `nlib::trade`, `nlib::book` in `<nlib/common.h>`), as do the
containers underneath: orders live in a `nlib::hive` (stable addresses), one
intrusive doubly linked list per side keeps them best price first and arrival
order within a price, and a `nlib::map` from order id to node makes trades and
cancels O(1) lookups.

A feed record *is* the book node. A resting `nlib::order` is copied into the
hive and chained through its own `prev` / `next` hooks, so nothing wraps it and
resting an order costs no separate allocation; the stored copy's `qty` tracks
the remaining quantity.

`nq::Orderbook` reacts to four calls:

| Handler | Meaning |
|---|---|
| `OnOrder()` | rests a limit-order add on its side |
| `OnTrade()` | shrinks both resting sides by the executed quantity |
| `OnCancel()` | shrinks an order by the cancelled quantity |
| `OnSnapshot()` | aggregates the top ten price levels per side into a `nlib::book` |

## Layout

```
include/nlib/          nlib submodule: containers and wire types
include/Orderbook.h    the interface — the contract of every member
src/Orderbook.cpp      its implementation
src/main.cpp           smoke test: replays a small feed and prints the book
```

Every member's contract is documented at its declaration in `Orderbook.h`;
the `.cpp` comments mechanism only. Read the header to use the book, the
source to change it.

## Building

```bash
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nqbook
```

`nqbook` runs a small feed-replay smoke test and prints the book after each
phase next to the expected lines.

## Releases

### v0.1.0 — 2026-08-16

The first working book: adds, trades, cancels, and snapshots over a replayed
feed.

- **Order storage on nlib.** `nlib::hive` owns the resting orders and keeps
  their addresses stable, one intrusive list per side gives price-time order,
  and `nlib::map` indexes by order id, so a trade or cancel is an O(1) lookup
  and an O(1) unlink.
- **`nlib::order` is the node.** The wrapper struct is gone: `order` carries
  the list hooks itself, which removed a per-order allocation and an
  indirection from every traversal.
- **Interface and implementation split.** `Orderbook.h` now declares; the
  bodies moved to `src/Orderbook.cpp`. The header reads as the contract
  instead of the mechanism.
- **The submodule moved to `include/nlib`**, so the include path is a single
  directory. Existing clones need
  `git submodule sync && git submodule update --init` once after pulling.
