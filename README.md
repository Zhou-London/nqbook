<p align="center"><img src="https://capsule-render.vercel.app/api?type=pulse&height=240&color=gradient&text=NQBook&fontSize=64&fontAlign=50&fontAlignY=42&desc=v0.1.1&descSize=22&descAlign=50&descAlignY=62" /></p>

<p align="center">
  <img alt="Version 0.1.1" src="https://img.shields.io/badge/version-0.1.1-blue" />
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Apache Arrow / Parquet" src="https://img.shields.io/badge/output-Parquet-50ABF1?logo=apache&logoColor=white" />
  <img alt="Plugins: dlopen" src="https://img.shields.io/badge/plugins-dlopen-orange" />
</p>

# NQBook

NQBook is an order book engine for MBO (market by order) data. It receives one event stream that covers every instrument. It keeps a separate order book per instrument. It reduces resting orders on trades and never simulates a match. Feature plugins read the books, and samplers turn the features into rows.

Data sources and feature computations are plugins. NQBook loads them with `dlopen` at startup. Adding a source or a feature never rebuilds NQBook. Plugins are written with [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk).

## Terms

| Term | Meaning |
|---|---|
| event | one `mbo` record: an add, cancel, modify, trade, or clear for one order |
| instrument | one tradable thing with its own order book, identified by a `uint32` id the adapter assigns |
| adapter | a plugin that produces the event stream, from a file or a live feed |
| feature | a plugin that computes numbers from a book |
| column | one number a feature produces, named `plugin.column` |
| snapshot column | a column that holds the value at the sampling instant |
| cumulative column | a column that holds a running total; the output row holds the increase since the previous row |
| sampler | a rule that decides when to read the features and emit a row: every N seconds, every N events, or every N trades |
| bar | the time span between two rows of a time sampler; a bar closes at a whole multiple of its length in event time |
| event time | the `ts` field of the event, UTC nanoseconds; NQBook has no other clock |

## Build

Requirements:

- CMake 3.28 or newer.
- A C++23 compiler: Apple clang 21 or GCC 14, or newer.
- Apache Arrow with Parquet, where CMake can find it. On macOS: `brew install apache-arrow`. On Debian and Ubuntu: `libarrow-dev` and `libparquet-dev`.
- GitHub access on the first configure. CMake fetches nlib, nqcppsdk, and CLI11.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

Outputs:

```
build/nqbook                                   the executable
build/plugins/features/example_features.so     example feature plugin: spread, mid, volume by side
build/plugins/adapters/replay.so               adapter that replays an .mbo file
build/plugins/adapters/synthetic.so            adapter that generates synthetic events, for tests
```

## First run

This command needs no data file. It generates 100000 events for 3 instruments, computes the example features, and writes one Parquet file per sampler into `out/`:

```bash
build/nqbook run --adapter build/plugins/adapters/synthetic.so \
                 --adapter-param count=100000 --adapter-param instruments=3 \
                 --plugin-dir build/plugins/features \
                 --sampler e=events:1000 \
                 --out-dir out
```

The report goes to stderr. The files are `out/e.parquet` and `out/instruments.parquet`.

## Layout

```
include/ src/          engine: Orderbook, PipelineTable, FeatureEngine, AdapterEngine, WriterEngine, Session
plugins/features/      feature plugin sources, one .so per subdirectory
plugins/adapters/      adapter plugin sources
shared/nqcppsdk/       the plugin SDK, fetched by CMake from GitHub at the pinned tag; not committed here
shared/nlib/           nlib, fetched the same way
build/_deps/           CLI11, fetched by CMake
tests/                 engine tests
```

## Commands

```
nqbook check       load plugins and samplers, print what would run, exit
nqbook run         consume an adapter's event stream through the books and plugins, write the rows
nqbook check-feed  build bare books with no features and report feed quality
nqbook help [subcommand]
```

Output streams:

| Command | stdout | stderr |
|---|---|---|
| `check` | the plugin, column, and sampler list, as text or JSON | error messages |
| `run` | nothing | the feed report, then one `wrote <path>` line per output file |
| `check-feed` | nothing | the feed report |

### check

```bash
build/nqbook check --plugin-dir build/plugins/features --sampler 1s=time:1s --sampler t=trades:1
build/nqbook check --plugin build/plugins/features/example_features.so --format json
```

`check` loads every plugin and calls `nq_init` once with a placeholder instrument. A plugin whose constructor throws fails here. The output lists plugins, columns, and samplers. `--format json` prints the same list as JSON.

### run

```bash
build/nqbook run --adapter build/plugins/adapters/replay.so --adapter-param path=day.mbo \
                 --plugin-dir build/plugins/features \
                 --sampler 1s=time:1s --sampler 5s=time:5s --sampler t=trades:1 \
                 --out-dir out
```

`run` starts the adapter thread. With `--out-dir` it also starts the writer thread. The main thread takes events from the adapter queue and applies them to the books. Each sampled row goes through a second queue to the writer thread. The writer appends the row to `<sampler>.parquet` while the run proceeds. At the end `run` prints the feed report, writes `instruments.parquet`, and closes the files. Without `--out-dir` it prints only the report.

| Option | Meaning |
|---|---|
| `--out-dir DIR` | directory that receives one file per sampler plus `instruments.parquet` |
| `--out-format parquet\|csv` | `parquet` streams rows to disk during the run. `csv` keeps rows in memory and writes `<sampler>.csv` at the end. Default `parquet` |
| `--row-group N` | rows per Parquet row group. Default 65536 |

Ctrl-C or SIGTERM stops a run cleanly. The adapter thread exits, the bars that have not closed yet are emitted, the output files are written, and the exit code is 130.

### check-feed

```bash
build/nqbook check-feed --adapter build/plugins/adapters/replay.so --adapter-param path=day.mbo
```

`check-feed` validates an adapter or a data file. The report has one `feed` line for the whole market, one `book` line that sums all books, and one line per instrument:

```
feed  events=300000 instruments=5 ts_regressions=0 instrument_unknown=0 stalls=135282 first_ts=... last_ts=... adapter_status=0
book  adds=165323 cancels=104686 modifies=8865 trades=21126 clears=0 | add_duplicate=0 cancel_unknown=2709 ...
  1      SYM1   events=60226 ts_regressions=0 orders=11606 bid_levels=1894 ask_levels=1890  adds=33042 ...
```

`run` prints the same report, plus a `writer` line when Parquet output is on.

| Field | Meaning |
|---|---|
| `ts_regressions` | events whose timestamp is below the previous one. A correct feed has 0 |
| `instrument_unknown` | instruments the adapter could not describe. A correct adapter has 0 |
| `stalls` | times the adapter waited because the queue to the books was full. A large value means the books are slower than the adapter. It is not an error |
| `adapter_status` | how the adapter ended: 0 on success, negative on error |
| `add_duplicate` | adds for an order id that is already in the book |
| `cancel_unknown` `modify_unknown` `trade_unknown` | operations on an order id the book does not hold |
| `trade_over_order` | trades that asked for more quantity than the traded order had left |
| `trade_over_level` | trades with `id` 0 that asked for more quantity than the price level had |
| `qty_invalid` | adds or trades whose quantity is not positive |
| `writer rows=… stalls=…` | `run` only. Rows the writer stored, and times the main thread waited because the writer queue was full |

## Options

### Plugins

| Option | Meaning |
|---|---|
| `--plugin PATH` | loads one `.so`. Repeatable. Loaded in command-line order |
| `--plugin-dir DIR` | loads every `.so` in the directory, sorted by file name |

The plugin name is the file name without `.so`. Output columns are named `plugin.column`.

A plugin reads parameters from a file next to the `.so` with the same name and the extension `.params` instead of `.so`. The file has one `key=value` per line. A `#` starts a comment:

```
# plugins/features/example_features.params
min_qty=1
```

### Adapter

| Option | Meaning |
|---|---|
| `--adapter PATH` | the `.so` that produces the event stream. Required |
| `--adapter-param KEY=VALUE` | one adapter parameter. Repeatable. Overrides the same key from the `.params` file |
| `--queue-capacity N` | capacity of the queue from the adapter to the books. Default 65536. Rounded up to a power of two |

An adapter reads a `.params` file in the same way as a plugin.

### Samplers

`--sampler NAME=TRIGGER:EVERY`. Repeatable. `NAME` becomes the output file name.

| TRIGGER | EVERY | Behavior |
|---|---|---|
| `time` | a duration with a unit: `1s`, `500ms`, `5m`, `1h` | closes a bar at every whole multiple of the duration in event time |
| `events` | a positive integer | emits one row every N events |
| `trades` | a positive integer | emits one row every N trades |

Example: `1s=time:1s` closes a bar at 10:00:00, 10:00:01, 10:00:02, and so on. The row for the bar from 10:00:00 to 10:00:01 carries `ts` 10:00:01.

Rules for time samplers:

- The `ts` of a row is the close time of its bar.
- The first bar of an instrument is skipped when the instrument's first event falls inside it.
- A bar with no events is skipped. `--fill-gaps` emits a row for it instead.
- The bar that has not closed yet when the stream ends is emitted.
- The clock is the event `ts`. A replay and a live run produce the same rows.

## Output

One Parquet file per sampler, ZSTD compressed, with the Arrow schema stored in the file:

| Column | Type |
|---|---|
| `instrument` | `uint32`, the id the adapter assigned |
| `ts` | `timestamp[ns, tz=UTC]` |
| one column per feature column | `float64`, named `plugin.column` |

`instruments.parquet` maps `instrument` to `symbol`, `tick_size`, and `lot_size`. The id is meaningful only within one run. Align runs by `symbol`.

A snapshot column holds the value at the sampling instant. A cumulative column holds the increase since the previous row of the same sampler, so the 1-second rows sum to the 5-second rows. Which kind a column is comes from the plugin; `check` prints it.

With `--out-format csv` each sampler becomes `<sampler>.csv` with the columns `instrument,symbol,ts,...`. `ts` is UTC nanoseconds as an integer.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 2 | a plugin or adapter failed to load: `dlopen` failed or an entry symbol is missing |
| 3 | the plugin's ABI hash differs from NQBook's. Rebuild the plugin with the current SDK |
| 4 | a plugin or adapter `init` failed, usually a missing or invalid parameter |
| 5 | a sampler definition is invalid or duplicated |
| 6 | a `.params` file or `--adapter-param` is malformed |
| 7 | the adapter ended with an error status |
| 8 | opening or writing the output failed |
| 130 | interrupted by SIGINT or SIGTERM |
| 100 and above | command-line usage error |

## Replay files

`replay.so` reads the `.mbo` format defined by NQCPPSDK: a 16-byte header, a 48-byte record per instrument, then 40-byte events to the end of the file. `Writer` in `nq/mbo_file.hpp` produces these files.
