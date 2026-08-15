# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`nqbook` — the limit order book service, C++23, namespace `nq`. A price-time
order book rebuilt from an order-by-order feed. See [README.md](README.md).

## Layout

```
nlib/                  git submodule: header-only containers and wire types
include/Orderbook.h    nq::Orderbook, header-only implementation
src/main.cpp           smoke test: replays a small feed and prints the book
```

Wire types (`nlib::order`, `nlib::trade`, `nlib::book`) come from
`<nlib/common.h>` — never redefine them here. To change one, edit nlib itself,
commit there, then bump the submodule pointer in this repo.

## Build

```bash
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nqbook
```

Testing is the smoke test in `main.cpp`, checked by eye against the expected
lines it prints — no gtest here (nlib carries the container test suites).

## Conventions

- Storage and lookup build on nlib: `nlib::hive` owns order nodes (stable
  addresses), intrusive lists order them, `nlib::map` indexes them by id.
- New structs go to nlib's `common.h` only if they are reusable wire types;
  implementation types (intrusive nodes etc.) stay in this repo.
- Comment style: Google C++ Style Guide, dense and short — the `cpp-comments`
  skill in `~/.claude/skills` codifies the rules.
