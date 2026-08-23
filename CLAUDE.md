# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

`nqbook` — the limit order book service, C++23, namespace `nq`. A persistent
process: a ZMQ feed thread, a book thread (one price-time order book per
instrument, snapshots on a fixed interval), and an Arrow/Parquet writer
thread, joined pairwise by lock-free `nlib::single_queue`s, plus a metrics
thread that samples the stages' cache-line-aligned counters and publishes raw
`nlib::metrics` records on its own conflating ZMQ PUB socket. Every interval,
endpoint, and queue size comes from `config/config.yaml`. See
[README.md](README.md).

## Layout

```
third_party/nlib/                  git submodule: containers, wire types, framing
third_party/fkYAML/                vendored single-header YAML parser, MIT, v0.4.4
include/Config.h                   nq::Config: the tunables and their defaults
src/Config.cpp                     reads config/config.yaml with fkYAML
config/config.yaml                 the tunables the service ships with
include/Orderbook.h                nq::Orderbook interface: the contract of every member
src/Orderbook.cpp                  its implementation
include/ParquetWriter.h            nq::ParquetWriter: one Parquet file per record type
src/ParquetWriter.cpp              every Arrow and Parquet call of the service
include/Threads/FeedThread.h       nq::FeedThread: FeedQueue and the feed stage's contract
src/Threads/FeedThread.cpp         feed thread: ZMQ SUB -> FeedQueue
include/Threads/BookThread.h       nq::BookThread: RecordQueue and the book stage's contract
src/Threads/BookThread.cpp         book thread: applies events, forwards them, snapshots
include/Threads/WriterThread.h     nq::WriterThread: the writer stage's contract
src/Threads/WriterThread.cpp       writer thread: hands records to nq::ParquetWriter
include/Threads/MetricsThread.h    nq::MetricsThread: the metric cells and the metrics stage's contract
src/Threads/MetricsThread.cpp      metrics thread: sampling -> nlib::metrics over its own ZMQ PUB
src/main.cpp                       entry point: config, wiring, and drain-ordered shutdown
compose.yml                        the service in the dev container, 5556 published
ui/                                Next.js dashboard over the metrics stream; npm on the host, not the container
```

Contracts live at the declarations in the headers; the `.cpp` files comment
mechanism only.

Wire types (`nlib::order`, `nlib::trade`, `nlib::level`, `nlib::book`), the
variants over them (`nlib::feed_event`, `nlib::record`), the framing tags, and
the `nlib::price_level` book node come from `<nlib/common.h>` — never redefine
them here. To change one, edit nlib itself,
commit there, then bump the submodule pointer in this repo.

## Build

C++ configures, builds, and runs only in the `dev` container (image built
from `Dockerfiles/cpp-dev`, which carries Arrow/Parquet and ZMQ); the host
has neither. A new system library means a package in that image first, which
dates every image already in use — prefer a header-only library vendored
under `third_party/`, the way fkYAML parses the config. The feed simulator
(`apps/util/feed_sim.py`) runs on the host with uv.

```bash
git submodule update --init
docker run --rm -v "$PWD":/work -w /work dev:latest bash -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build -j'
```

`compose.yml` runs the built binary in that image with `5556:5556`
published (`docker compose up`); it does not build. The dashboard is host-side
npm: `cd ui && npm run dev`, port 3000 either way.

The nlib submodule sits at `third_party/nlib`. A clone holding it at any
other path needs `git submodule sync && git submodule update --init` once —
until that runs, `add_subdirectory(third_party/nlib)` fails to configure.

Testing is end to end: publish a feed on the host — `uv run feed_sim.py` for a
synthetic one, `md/kraken` for live Kraken level3 — run the service in the
container (`--add-host=host.docker.internal:host-gateway`, or
`docker compose up`), stop it with SIGTERM, and inspect `data_out/*.parquet`
(e.g. `uv run --with pyarrow python`). The dashboard is the live check on the
pipeline while it runs. No gtest here (nlib carries the container
test suites).

## Conventions

- Tunables live in `config/config.yaml` and reach a stage through its
  constructor. `NQBOOK_CONFIG` names the file; the command line and the
  environment override the endpoints and the output directory. A new tunable
  is a `nq::Config` field with a default, never a constant in a hot path.
  `--l2` is a run mode rather than a tunable: the command line carries it, and
  `main` passes it straight to the feed stage.
- Storage and lookup build on nlib: `nlib::hive` owns order nodes (stable
  addresses), intrusive lists queue them inside `nlib::price_level` levels,
  and `nlib::map` indexes them by id.
- Threads never share state: each stage pair communicates through exactly one
  `nlib::single_queue` (SPSC), so no stage takes a lock. Keep it that way.
- Monitoring never reads pipeline data. Hot paths write single-writer,
  cache-line-aligned `Metric` cells (relaxed load + store); the metrics
  thread only samples cells and publishes. A new metric is a new cell, not a
  lock or a shared structure.
- Parquet writing batches rows: `Reserve` a batch of builder capacity, fill it
  with `UnsafeAppend`, write the batch as one row group. Every Arrow and
  Parquet call belongs in `ParquetWriter`; `WriterThread` only moves records
  into it. A new record type means one `Open*` and one `Append*` pair listing
  the same columns in the same order.
- The book runs on order flow or on `nlib::level` records, never on both.
  `--l2` picks the mode at startup: the feed stage accepts level frames and
  drops order flow, and `OnLevel()` writes a level's absolute quantity into an
  aggregate `nlib::price_level` (empty order queue). Keep the two paths
  separate rather than merging a level into an order-backed book.
- New structs go to nlib's `common.h` only if they are reusable wire types;
  implementation types (intrusive nodes etc.) stay in this repo.
- Prose goes through the `humandoc` skill: load it before writing or editing
  any comment, docstring, README line, or commit message. Comments stay
  short — one line wherever one line carries the fact, and the `.cpp` files
  comment mechanism only.
- A scheduled task owns git and the repository-wide documents. It commits the
  repos under `~/Containers/apps` and refreshes README.md and CLAUDE.md, so
  leave a change uncommitted, report what it touched, and edit those two files
  only when asked.
