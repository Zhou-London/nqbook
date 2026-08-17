<img src="https://capsule-render.vercel.app/api?type=waving&height=400&text=Orderbook&fontAlign=80&fontAlignY=40&color=gradient" />

<p align="center">
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Depends on nlib" src="https://img.shields.io/badge/submodule-nlib-4c1?logo=git&logoColor=white" />
</p>

Project of NowQuant.

`nqbook` — the limit order book service. Namespace `nq`, C++23.

A price-time-priority book for one instrument, rebuilt from an order-by-order
feed. It does not match: crossing orders rest until the feed reports their
trades.

The process is a persistent three-thread pipeline, each pair of stages joined
by one lock-free `nlib::single_queue`:

| Thread | Does |
|---|---|
| feed | receives adapted wire records over a ZMQ SUB socket |
| book | applies each event to `nq::Orderbook`, forwards it, snapshots every 3 s |
| writer | batches records into Arrow and writes one Parquet file per type under `data_out/` |

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
include/Orderbook.h    nq::Orderbook — the contract of every member
src/Orderbook.cpp      its implementation
include/Pipeline.h     the pipeline: queue types, wire framing, thread contracts
src/Feed.cpp           feed thread: ZMQ SUB → FeedQueue
src/Book.cpp           book thread: applies events, snapshots → RecordQueue
src/Writer.cpp         writer thread: Arrow batches → Parquet files
src/main.cpp           entry point: thread wiring and drain-ordered shutdown
```

Every contract is documented at its declaration in the headers; the `.cpp`
files comment mechanism only. Read the headers to use a component, the
sources to change one.

## Building and running

C++ builds and runs in the `dev` container (built from
`Dockerfiles/cpp-dev`); the feed simulator runs on the host with
[uv](https://docs.astral.sh/uv/).

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

`nqbook [feed_endpoint] [out_dir]` defaults to
`tcp://host.docker.internal:5555` (overridable via `NQBOOK_FEED_ENDPOINT`)
and `data_out`. SIGINT or SIGTERM stops it; shutdown drains the queues
upstream-first, so every received record reaches the Parquet files, one
zstd-compressed file per record type stamped with the start time.

## Releases

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
