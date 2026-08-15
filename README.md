<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
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

`nq::Orderbook` reacts to four calls:

| Handler | Meaning |
|---|---|
| `OnOrder()` | rests a limit-order add on its side |
| `OnTrade()` | shrinks both resting sides by the executed quantity |
| `OnCancel()` | shrinks an order by the cancelled quantity |
| `OnSnapshot()` | aggregates the top ten price levels per side into a `nlib::book` |

## Building

```bash
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nqbook
```

`nqbook` runs a small feed-replay smoke test and prints the book after each
phase next to the expected lines.
