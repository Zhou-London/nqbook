# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`nqbook` — the limit order book service, C++23, namespace `nq`. Currently a
skeleton: it builds and links, but no handler is implemented. See
[README.md](README.md).

## Layout

```
include/Orderbook.h        the nq::Orderbook interface
include/common/common.h    wire types: Side, Order, Trade
src/Orderbook.cpp          implementation (empty)
src/main.cpp               entry point (empty)
```

Both `src/` and `include/` are on the include path, so headers are included as
`<Orderbook.h>` and `<common/common.h>`.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

There is no test target yet. Adding one means GoogleTest via `FetchContent` and
a `tests/` subdirectory — mirror the arrangement in
[nlib](https://github.com/Zhou-London/nlib), which this project is meant to
build on.

## Known gaps

Real work, not stylistic nits — worth fixing on the way past:

- Neither header has an include guard or `#pragma once`. Nothing includes them
  twice yet, so it does not bite today.
- `src/Orderbook.cpp` is empty while `Orderbook.h` defines its handlers inline;
  decide which side the implementation lives on before filling them in.
- `CMakeLists.txt` sets `EXPORT_COMPILE_COMMANDS`, not
  `CMAKE_EXPORT_COMPILE_COMMANDS`, so no `compile_commands.json` is written and
  clangd runs blind.

## Conventions

- Wire types stay fixed-layout and trivially copyable — fixed `char symbol[16]`,
  unsigned integer prices and quantities, no `std::string`, no floating point.
- Comment style: Google C++ Style Guide, dense and short — the `cpp-comments`
  skill in `~/.claude/skills` codifies the rules.
