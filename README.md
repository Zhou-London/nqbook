<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="Version 0.5.0" src="https://img.shields.io/badge/version-0.5.0-blue" />
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Depends on nlib" src="https://img.shields.io/badge/submodule-nlib-4c1?logo=git&logoColor=white" />
</p>

Project of NowQuant.

`nqbook` — the limit order book service. Namespace `nq`, C++23.

Price-time-priority books, one per instrument, rebuilt from an order-by-order
feed. The books do not match: crossing orders rest until the feed reports
their trades.

The process is a persistent pipeline of three stages joined by lock-free
`nlib::single_queue`s, plus a monitor on the side:

| Thread | Does |
|---|---|
| feed | receives framed wire records over a ZMQ SUB socket, stamps `recv_ns`; `feed.l2` selects level records over order flow |
| book | applies each event to its instrument's `nq::Orderbook`, forwards it, snapshots every book once per `snapshot_period_ms` |
| writer | batches records into Arrow and writes one Parquet file per type — orders, trades, cancels, levels, books — under `data_out/` |
| metrics | samples the stages' counters once per `sample_period_ms` and publishes each sample on its own ZMQ PUB socket |

## Shape

Wire types come from the [nlib](https://github.com/Zhou-London/nlib) submodule
(`nlib::order`, `nlib::trade`, `nlib::cancel`, `nlib::level`, `nlib::book` in
`<nlib/common.h>`, plus the framing tags and the `nlib::feed_event` and
`nlib::record` variants the queues carry), as do the containers underneath:
orders live in a `nlib::hive`
(stable addresses), each price keeps a `nlib::price_level` whose intrusive FIFO
queue holds the orders in arrival order, and a `nlib::map` from order id to node
makes trades and cancels O(1) lookups. A `std::map` per side keeps the levels in
price order, which is what a snapshot walks.

A feed record *is* the book node. A resting `nlib::order` is copied into the
hive and chained through its own `prev` / `next` hooks, so nothing wraps it and
resting an order costs no separate allocation; the stored copy's `qty` tracks
the remaining quantity.

An L2 feed skips the orders. A `level` record carries the level's total resting
quantity, so `OnLevel()` writes that number straight into an aggregate
`nlib::price_level` — one with an empty order queue. A book runs on order flow
or on level records, never on both.

`nq::Orderbook` reacts to five calls:

| Handler | Meaning |
|---|---|
| `OnOrder()` | rests a limit-order add, or applies a modify's `new_qty` to the resting order, keeping priority while the price is unchanged |
| `OnTrade()` | shrinks both resting sides by the executed quantity |
| `OnCancel()` | takes a `nlib::cancel` record's quantity out of an order; it leaves once nothing remains |
| `OnLevel()` | sets a price level's total quantity from an absolute L2 record; `qty <= 0` erases the level |
| `OnSnapshot()` | aggregates the top `nlib::snapshot_depth` price levels per side into a `nlib::book` |

Each book also answers `size()` and `MemoryBytes()` — the gauges behind the
metrics stream.

## Layout

```
third_party/nlib/                  nlib submodule: containers, wire types, framing
third_party/fkYAML/                vendored single-header YAML parser, MIT, v0.4.4
include/Config.h                   nq::Config — the tunables and their defaults
src/Config.cpp                     reads config/config.yaml with fkYAML
config/config.yaml                 the tunables the service ships with
include/Orderbook.h                nq::Orderbook — the contract of every member
src/Orderbook.cpp                  its implementation
include/ParquetWriter.h            nq::ParquetWriter — one Parquet file per record type
src/ParquetWriter.cpp              every Arrow and Parquet call of the service
include/Threads/FeedThread.h       nq::FeedThread — FeedQueue and the feed stage's contract
src/Threads/FeedThread.cpp         feed thread: ZMQ SUB → FeedQueue
include/Threads/BookThread.h       nq::BookThread — RecordQueue and the book stage's contract
src/Threads/BookThread.cpp         book thread: applies events, snapshots → RecordQueue
include/Threads/WriterThread.h     nq::WriterThread — the writer stage's contract
src/Threads/WriterThread.cpp       writer thread: hands records to nq::ParquetWriter
include/Threads/MetricsThread.h    nq::MetricsThread — the metric cells and the metrics stage's contract
src/Threads/MetricsThread.cpp      metrics thread: samples the cells, publishes nlib::metrics over ZMQ
src/main.cpp                       entry point: config, thread wiring, drain-ordered shutdown
compose.yml                        the service in the dev container, metrics port published
ui/                                Next.js dashboard over the metrics stream (runs on the host)
docs/releases/                     one release note per version
```

Every contract is documented at its declaration in the headers; the `.cpp`
files comment mechanism only. Read the headers to use a component, the
sources to change one.

Comments follow the `humandoc` skill in `~/.claude/skills` and stay short:
one line wherever one line carries the fact. A scheduled task owns git and
the repository-wide documents — it commits the working tree and refreshes
README.md and CLAUDE.md, so a change lands uncommitted.

## Building and running

C++ builds and runs in the `dev` container (built from
`Dockerfiles/cpp-dev`), which supplies Arrow, Parquet, and cppzmq; YAML
parsing is [fkYAML](https://github.com/fktn-k/fkYAML), one vendored header
under `third_party/`, so the image needs no YAML package.

```bash
git submodule update --init
docker run --rm -v "$PWD":/work -w /work dev:latest bash -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build -j'
```

```bash
# In the container: receive the feed from the host, book it, persist it.
docker run --rm --add-host=host.docker.internal:host-gateway \
    -v "$PWD":/work -w /work dev:latest ./build/nqbook
```

[`compose.yml`](compose.yml) is the same run, spelled once: the `dev` image
over the working tree with `5556:5556` published, so the dashboard on the host
reaches the metrics socket without repeating the flags.

```bash
docker compose up          # equivalently, and Ctrl-C to drain and stop
```

[`config/config.yaml`](config/config.yaml) holds the tunables: both endpoints,
`feed.l2`, the output directory, the two queue capacities, the writer's batch
size, the snapshot interval, and the metrics sample interval. `nqbook` takes
no arguments and reads no environment variable; a key left out of the file
keeps its default, and a malformed file stops the process at startup.
Setting `feed.l2: true` switches the feed to L2 mode: level and trade
records, order flow dropped. Add `-p 5556:5556` to reach the metrics socket
from the host.
SIGINT or SIGTERM stops the service; shutdown drains the queues
upstream-first, so every received record reaches the Parquet files, one
zstd-compressed file per record type stamped with the start time.

The metrics stream is one raw `nlib::metrics` record (136 bytes, host
layout) per sample interval on a conflating PUB socket: nothing queues beyond the
newest sample, so a subscriber always reads live state, never a backlog.
Counters — feed messages/bytes/records, book events and timed-apply latency,
writer rows — are cumulative; difference consecutive samples for rates. The
book gauges (instruments, resting orders, memory) are instantaneous. The hot
paths only write single-writer cache-line-aligned counters (a relaxed
load + store, ~1 ns); the metrics thread does all the sampling and
publishing on its own socket and context, and never touches pipeline data.

[`ui/`](ui/README.md) renders that stream live in the browser — a Next.js
app that bridges ZMQ to Server-Sent Events and charts the rolling minute,
turning each counter into a per-second rate over a sliding 1 s window of
arrival times:

```bash
cd ui && npm install && npm run dev   # http://localhost:3000
```

## Releases

One note per version under [`docs/releases/`](docs/releases/). Each note
lists what changed, the nlib pin it ships against, and any wire or Parquet
layout that moved.

| Version | Date | Highlights |
|---|---|---|
| [v0.5.0](docs/releases/v0.5.0.md) | 2026-09-09 | Cancels are their own wire record, `feed.l2` moves into the config file, book snapshots narrow to the best level, logging moves to spdlog |
| [v0.4.1](docs/releases/v0.4.1.md) | 2026-08-26 | Contracts restated at every declaration, nlib pinned at v0.4.0, project version matched |
| [v0.4.0](docs/releases/v0.4.0.md) | 2026-08-23 | `config/config.yaml` holds the tunables, one file per stage, `--l2` runs the book on aggregated depth |
| [v0.3.1](docs/releases/v0.3.1.md) | 2026-08-22 | Partial cancels via `cancel_qty`, `new_qty` for modifies, two new orders columns |
| [v0.3.0](docs/releases/v0.3.0.md) | 2026-08-18 | Live level3 feed, metrics thread and socket, the `ui/` dashboard, `compose.yml` |
| [v0.2.0](docs/releases/v0.2.0.md) | 2026-08-17 | Three threads and two queues, ZMQ SUB feed, Parquet persistence, draining shutdown |
| [v0.1.0](docs/releases/v0.1.0.md) | 2026-08-16 | First working book: adds, trades, cancels, and snapshots over a replayed feed |
