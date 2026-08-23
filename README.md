<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Depends on nlib" src="https://img.shields.io/badge/submodule-nlib-4c1?logo=git&logoColor=white" />
</p>

Project of NowQuant.

`nqbook` — the limit order book service. Namespace `nq`, C++23.

Price-time-priority books, one per instrument, rebuilt from an order-by-order
feed — `md/kraken` in [util](https://github.com/Zhou-London/nq-util) publishes
Kraken's spot level3 feed in exactly this shape. The books do not match:
crossing orders rest until the feed reports their trades.

The process is a persistent pipeline of three stages joined by lock-free
`nlib::single_queue`s, plus a monitor on the side:

| Thread | Does |
|---|---|
| feed | receives framed wire records over a ZMQ SUB socket, stamps `recv_ns`; `--l2` selects level records over order flow |
| book | applies each event to its instrument's `nq::Orderbook`, forwards it, snapshots every book once per `snapshot_period_ms` |
| writer | batches records into Arrow and writes one Parquet file per type — orders, trades, levels, books — under `data_out/` |
| metrics | samples the stages' counters once per `sample_period_ms` and publishes each sample on its own ZMQ PUB socket |

## Shape

Wire types come from the [nlib](https://github.com/Zhou-London/nlib) submodule
(`nlib::order`, `nlib::trade`, `nlib::level`, `nlib::book` in `<nlib/common.h>`,
plus the framing tags and the `nlib::feed_event` and `nlib::record` variants the
queues carry), as do the containers underneath: orders live in a `nlib::hive`
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

`nq::Orderbook` reacts to seven calls:

| Handler | Meaning |
|---|---|
| `OnOrder()` | rests a limit-order add on its side |
| `OnTrade()` | shrinks both resting sides by the executed quantity |
| `OnCancel()` | takes `cancel_qty` out of an order; it leaves once nothing remains |
| `OnModify()` | sets an order's remaining quantity to `new_qty`, keeping priority while the price is unchanged |
| `OnClear()` | drops every resting order, ahead of a feed snapshot replay |
| `OnLevel()` | sets a price level's total quantity from an absolute L2 record; `qty <= 0` erases the level |
| `OnSnapshot()` | aggregates the top ten price levels per side into a `nlib::book` |

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
under `third_party/`, so the image needs no YAML package. The feed simulator
runs on the host with [uv](https://docs.astral.sh/uv/).

```bash
git submodule update --init
docker run --rm -v "$PWD":/work -w /work dev:latest bash -c \
    'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build -j'
```

```bash
# On the host: publish a repeated virtual order on tcp://*:5555.
cd ../util && uv run feed_sim.py
```

```bash
# In the container: receive it, book it, persist it.
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
the output directory, the two queue capacities, the writer's batch size, the
snapshot interval, and the metrics sample interval. `NQBOOK_CONFIG` names another file, a key left out
keeps its default, and a malformed file stops the process at startup.
`nqbook [feed_endpoint] [out_dir] [metrics_endpoint]` and the
`NQBOOK_FEED_ENDPOINT` / `NQBOOK_METRICS_ENDPOINT` variables override the file,
arguments first. `--l2` sits anywhere among those arguments and switches the
feed to L2 mode: level, trade, and clear records, order flow dropped. Add
`-p 5556:5556` to reach the metrics socket from the host.
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

### v0.4.0 — 2026-08-23

The service stopped hard-coding its own shape: the tunables moved to a config
file, the pipeline split one file per stage, and the book learned to run on an
L2 feed.

- **`config/config.yaml` holds every tunable** — both endpoints, the output
  directory, the two queue capacities, the batch size, the snapshot interval,
  and the metrics sample interval. `nq::Config` carries the defaults, so a key
  left out of the file keeps working and a malformed file stops the process at
  startup rather than halfway through a run. `NQBOOK_CONFIG` names another
  file; the command line and the two endpoint variables still override it.
  Parsing is [fkYAML](https://github.com/fktn-k/fkYAML), one MIT header
  vendored under `third_party/`, so the `dev` image needs no YAML package.
- **`--l2` runs the book on aggregated depth.** The feed stage accepts
  `nlib::level` records and drops order flow, `Orderbook::OnLevel()` writes a
  level's absolute quantity into an aggregate `nlib::price_level`, and a
  quantity of 0 or less erases the level. Snapshots and Parquet output work the
  same in either mode. `uv run feed_sim.py --l2` in
  [util](https://github.com/Zhou-London/nq-util) publishes a matching feed.
- **One file per stage.** `Pipeline.h` and `Metrics.h` are gone; each stage now
  owns a header and a source under `Threads/`, holding its own queue type and
  contract. `src/` mirrors that layout.
- **`nq::ParquetWriter` is a class**, not code inside the writer thread. Every
  Arrow and Parquet call of the service lives in one file; `WriterThread` only
  moves records into it and closes it on shutdown. A fourth file, `levels`,
  joins orders, trades, and books.
- **The framing moved to nlib.** The tag bytes, the `feed_event` and `record`
  variants, and the `price_level` node were declared here and are now declared
  once in `<nlib/common.h>`, so publisher and receiver read the same
  constants.
- **The nlib submodule moved to `third_party/nlib`**, next to fkYAML. Existing
  clones need `git submodule sync && git submodule update --init` once;
  until that runs, `add_subdirectory(third_party/nlib)` fails to configure.
- **nlib bumped** to `4fc74f1` for `nlib::level`, the framing, the variants,
  and `nlib::price_level`. `nlib::metrics` grows from 120 to 136 bytes with
  `feed_levels` and `writer_levels`, so the dashboard and the service must be
  updated together.
- **The dashboard tracks both new counters**, tiling level rates beside the
  order and trade rates.

### v0.3.1 — 2026-08-22

Cancels became partial, following nlib's split of the action quantities.

- **`OnCancel()` shrinks rather than erases.** It takes `o.cancel_qty` out of
  the resting order and removes it only when nothing remains — the same path
  `OnTrade()` already used. Kraken reports one quantity per delete, so a full
  cancel is just the case where that quantity is the whole remainder.
- **`OnModify()` reads `o.new_qty`** instead of overloading `o.qty`, which now
  means the resting quantity and nothing else.
- **The orders Parquet file gains `cancel_qty` and `new_qty`**, both after
  `qty`. Readers that index columns by position rather than by name need
  updating; files written before this release have ten columns, not twelve.
- **nlib bumped** to `1377010` for those fields. `nlib::order` grows from 72
  to 88 bytes, so a publisher on the old layout and this service cannot be
  mixed — rebuild both.

### v0.3.0 — 2026-08-18

`nqbook` took a real exchange feed and grew an instrument panel: a monitoring
thread, a metrics socket, and a dashboard reading it live.

- **The feed is Kraken's spot level3**, normalized by `md/kraken` in
  [util](https://github.com/Zhou-London/nq-util). The book handles the full
  action set the feed uses — `modify` keeps queue priority while the price is
  unchanged, `clear` drops an instrument's resting orders ahead of a snapshot
  replay — and prices and quantities are fixed point at nlib's scales.
- **A metrics thread**, off the hot path. The stages write single-writer,
  cache-line-aligned counters (a relaxed load plus store, ~1 ns); the monitor
  samples them every 100 ms and publishes each sample as a raw 120-byte
  `nlib::metrics` record on its own ZMQ PUB socket, `conflate` set, so a
  subscriber always reads live state rather than a backlog. Monitoring never
  reads pipeline data and never takes a lock.
- **Apply latency is sampled**, 1 in 1024 applies, as cumulative nanoseconds
  plus a count — the ratio is the mean per timed apply, at no cost to the
  untimed ones.
- **[`ui/`](ui/README.md), the dashboard**, moved here from `util` so the
  service and its instrument panel version together. A Next.js route holds one
  conflated ZMQ SUB per browser client and relays samples over Server-Sent
  Events; the page keeps a rolling minute and turns counters into per-second
  rates over a sliding 1 s window of arrival times, tiled by stage. Runs on
  the host with Node, on port 3000.
- **[`compose.yml`](compose.yml)** runs the service in the `dev` container
  with the metrics port published, replacing the flag-by-flag `docker run`.
- **`NQBOOK_METRICS_ENDPOINT`** and a third positional argument configure the
  metrics socket; it defaults to `tcp://0.0.0.0:5556`.
- **nlib bumped** to `d248297`: the split `event_ns` / `recv_ns` times, the
  fixed-point `qty_scale`, the `modify` and `clear` actions, `nlib::metrics`,
  and `map::capacity()` for the memory gauge.

### v0.2.0 — 2026-08-17

`nqbook` stopped being a smoke test and became a service: it now takes a live
feed and persists everything it books.

- **Three threads, two queues.** `Pipeline.h` declares the stage contracts —
  feed, book, writer — and joins each pair with one `nlib::single_queue`
  (SPSC), so the process runs lock-free end to end. A full queue makes the
  producer wait rather than drop, so backpressure from storage reaches ZMQ.
- **The feed is a ZMQ SUB socket.** Framing is one tag byte followed by the
  record in host layout; anything that matches no tag-plus-size is dropped.
  `order::prev` / `order::next` arrive as wire bytes and are discarded — the
  hooks are book-owned state.
- **Parquet persistence.** The writer keeps one sink per record type, buffers
  1024 rows into Arrow builders, and writes each batch as one zstd-compressed
  row group. Files land in `data_out/`, stamped with the writer start time,
  with the Arrow schema embedded so readers restore the fixed-size lists in
  `book`. A storage error aborts the process; running on without persistence
  would lose data silently.
- **Snapshots are on the wall clock**, every 3 s, whether or not the book saw
  an event.
- **Shutdown drains.** SIGINT or SIGTERM stops the stages upstream-first —
  feed, then book, then writer — and each drains its input before returning,
  so every received record reaches a file.
- **Configuration** is `nqbook [feed_endpoint] [out_dir]`, with the endpoint
  falling back to `NQBOOK_FEED_ENDPOINT` and then to
  `tcp://host.docker.internal:5555`.
- **The build needs Arrow, Parquet, and cppzmq**, which only the `dev`
  container carries — the host can no longer configure this project.
- **nlib bumped** to `84ee2cc`: `order::side` and `trade::side` are now
  declared `nlib::side`, without which this repository does not compile under
  GCC 13.

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
