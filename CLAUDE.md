# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`nqbook` — the limit order book service, C++23, namespace `nq`. A persistent
process: a ZMQ feed thread, a book thread (one price-time order book per
instrument, snapshots every 3 s), and an Arrow/Parquet writer thread, joined
pairwise by lock-free `nlib::single_queue`s, plus a metrics thread that
samples the stages' cache-line-aligned counters every 100 ms and publishes
raw `nlib::metrics` records on its own conflating ZMQ PUB socket. See
[README.md](README.md).

## Layout

```
include/nlib/          git submodule: header-only containers and wire types
include/Orderbook.h    nq::Orderbook interface: the contract of every member
src/Orderbook.cpp      its implementation
include/Pipeline.h     queue types, wire framing, and the stage thread contracts
include/Metrics.h      single-writer metric cells + the monitor thread contract
src/Feed.cpp           feed thread: ZMQ SUB -> FeedQueue
src/Book.cpp           book thread: applies events, forwards them, snapshots
src/Writer.cpp         writer thread: batched Parquet under data_out/
src/Metrics.cpp        metrics thread: 100 ms sampling -> nlib::metrics over its own ZMQ PUB
src/main.cpp           entry point: wiring and drain-ordered shutdown
compose.yml            the service in the dev container, 5556 published
ui/                    Next.js dashboard over the metrics stream; npm on the host, not the container
```

Contracts live at the declarations in the headers; the `.cpp` files comment
mechanism only.

Wire types (`nlib::order`, `nlib::trade`, `nlib::book`) come from
`<nlib/common.h>` — never redefine them here. To change one, edit nlib itself,
commit there, then bump the submodule pointer in this repo.

## Build

C++ configures, builds, and runs only in the `dev` container (image built
from `Dockerfiles/cpp-dev`, which carries Arrow/Parquet and ZMQ); the host
has neither. The feed simulator (`apps/util/feed_sim.py`) runs on the
host with uv.

```bash
git submodule update --init
docker run --rm -v "$PWD":/work -w /work dev:latest bash -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build -j'
```

`compose.yml` runs the built binary in that image with `5556:5556`
published (`docker compose up`); it does not build. The dashboard is host-side
npm: `cd ui && npm run dev`, port 3000 either way.

A clone made before the submodule moved to `include/nlib` needs
`git submodule sync` first; without it the old path stays checked out and the
`add_subdirectory(include/nlib)` line fails to configure.

Testing is end to end: publish a feed on the host — `uv run feed_sim.py` for a
synthetic one, `md/kraken` for live Kraken level3 — run the service in the
container (`--add-host=host.docker.internal:host-gateway`, or
`docker compose up`), stop it with SIGTERM, and inspect `data_out/*.parquet`
(e.g. `uv run --with pyarrow python`). The dashboard is the live check on the
pipeline while it runs. No gtest here (nlib carries the container
test suites).

## Conventions

- Storage and lookup build on nlib: `nlib::hive` owns order nodes (stable
  addresses), intrusive lists order them, `nlib::map` indexes them by id.
- Threads never share state: each stage pair communicates through exactly one
  `nlib::single_queue` (SPSC), so no stage takes a lock. Keep it that way.
- Monitoring never reads pipeline data. Hot paths write single-writer,
  cache-line-aligned `Metric` cells (relaxed load + store); the metrics
  thread only samples cells and publishes. A new metric is a new cell, not a
  lock or a shared structure.
- Parquet writing batches rows: `Reserve` a batch of builder capacity, fill it
  with `UnsafeAppend`, write the batch as one row group.
- New structs go to nlib's `common.h` only if they are reusable wire types;
  implementation types (intrusive nodes etc.) stay in this repo.
- Comment style: Google C++ Style Guide, dense and short — the `cpp-comments`
  skill in `~/.claude/skills` codifies the rules.
