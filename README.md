# NQBook

NQBook is an order book engine for MBO (market by order) data. It routes one market-wide stream of `mbo` events to a separate order book per instrument, reduces orders on trades instead of simulating a match, feeds each book to dynamically loaded feature plugins, and emits feature rows at the cadence of each sampler.

Data sources and feature computations are plugins. NQBook loads them with `dlopen` at startup, so adding a source or a feature never rebuilds NQBook. Plugins are written with [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk).

## Build

Requires CMake 3.28+, a C++23 compiler (Apple clang 21 or GCC 14 and later), and GitHub access on the first configure to fetch nlib, nqcppsdk, and CLI11.

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

## Layout

```
include/ src/          engine: Orderbook, PipelineTable, FeatureEngine, AdapterEngine, Session
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
nqbook run         consume an adapter's event stream through the books and plugins, write CSV at the end
nqbook check-feed  build bare books with no features and report feed quality
nqbook help [subcommand]
```

### check

```bash
nqbook check --plugin-dir build/plugins/features --sampler 1s=time:1s --sampler t=trades:1
nqbook check --plugin build/plugins/features/example_features.so --format json
```

`check` loads every plugin and calls `nq_init` once with a placeholder instrument, so a plugin constructor error surfaces here. The output lists plugins, columns, and samplers. `--format json` produces machine-readable output.

### run

```bash
nqbook run --adapter build/plugins/adapters/replay.so --adapter-param path=day.mbo \
           --plugin-dir build/plugins/features \
           --sampler 1s=time:1s --sampler 5s=time:5s --sampler t=trades:1 \
           --out-dir out
```

`run` starts the adapter thread, consumes events from the queue on the main thread, prints the feed report to stderr at the end, then writes one `<sampler>.csv` per sampler under `--out-dir`. Without `--out-dir` it prints only the report.

Ctrl-C or SIGTERM stops a run cleanly: the adapter thread exits, open bars are emitted, the CSV files are written, and the exit code is 130.

### check-feed

```bash
nqbook check-feed --adapter build/plugins/adapters/replay.so --adapter-param path=day.mbo
```

`check-feed` validates an adapter or a data file. The first line of the report counts the whole market, the second line sums the book operations and violations, and each instrument follows on its own line:

```
feed  events=300000 instruments=5 ts_regressions=0 instrument_unknown=0 stalls=135282 first_ts=... last_ts=... adapter_status=0
book  adds=165323 cancels=104686 modifies=8865 trades=21126 clears=0 | add_duplicate=0 cancel_unknown=2709 ...
  1      SYM1   events=60226 ts_regressions=0 orders=11606 bid_levels=1894 ask_levels=1890  adds=33042 ...
```

| Field | Meaning |
|---|---|
| `ts_regressions` | events whose timestamp is below the previous one; should be 0 |
| `instrument_unknown` | instruments the adapter could not describe; should be 0 |
| `stalls` | times the adapter waited on a full queue; measures consumer speed only |
| `adapter_status` | adapter end status, 0 on success, negative on error |
| `add_duplicate` | adds for an order id that already rests in the book |
| `cancel_unknown` `modify_unknown` `trade_unknown` | operations on an order id the book does not hold |
| `trade_over_order` | trade quantity above the resting quantity of the traded order |
| `trade_over_level` | trade reduced by price level, and the level held less than the trade quantity |
| `qty_invalid` | add or trade with a quantity that is not positive |

## Options

### Plugins

| Option | Meaning |
|---|---|
| `--plugin PATH` | loads one `.so`; repeatable, loaded in command-line order |
| `--plugin-dir DIR` | loads every `.so` in the directory, sorted by file name |

The plugin name is the file name without `.so`, and output columns are named `plugin.column`. A `.params` file next to the `.so` with the same stem supplies plugin parameters, one `key=value` per line, `#` starts a comment:

```
# plugins/features/example_features.params
min_qty=1
```

### Adapter

| Option | Meaning |
|---|---|
| `--adapter PATH` | the `.so` that produces the event stream; required |
| `--adapter-param KEY=VALUE` | adapter parameter; repeatable; overrides the same key from the `.params` file |
| `--queue-capacity N` | capacity of the adapter-to-book queue, default 65536, rounded up to a power of two |

An adapter also reads a `.params` file with its own stem.

### Samplers

`--sampler NAME=TRIGGER:EVERY`, repeatable.

| TRIGGER | EVERY | Behavior |
|---|---|---|
| `time` | duration with unit: `1s`, `500ms`, `5m`, `1h` | aligns to a grid in event time and emits one row per boundary |
| `events` | positive integer | emits one row every N events |
| `trades` | positive integer | emits one row every N trades |

`--fill-gaps` makes time samplers emit a row for bars with no events. By default empty bars are skipped.

Time sampler rules: a bar carries its close time as `ts`; the first partial bar of an instrument is skipped; the open bar at the end of the stream is emitted. The clock is the event `ts`, so replay and live runs produce the same rows.

## Output

One CSV per sampler:

```
instrument,symbol,ts,example_features.spread,example_features.mid,example_features.buy_vol,...
1,SYM1,2000000000,0.2,100.6,644,643,1287
```

`instrument` is the id the adapter assigned and is meaningful only within one run; align runs by `symbol`. `ts` is UTC nanoseconds. Snapshot columns hold the value at the sampling instant. Cumulative columns hold the increase since the previous row, so the 1-second rows sum to the 5-second rows.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 2 | plugin or adapter failed to load: `dlopen` failed or an entry symbol is missing |
| 3 | the plugin's ABI hash differs from NQBook's; rebuild the plugin with the current SDK |
| 4 | plugin or adapter `init` failed, usually a missing or invalid parameter |
| 5 | sampler definition is invalid or duplicated |
| 6 | a `.params` file or `--adapter-param` is malformed |
| 7 | the adapter ended with an error status |
| 8 | writing the output failed |
| 130 | interrupted by SIGINT or SIGTERM |
| 100 and above | command-line usage error |

## Replay files

`replay.so` reads the `.mbo` format defined by NQCPPSDK: a 16-byte header, a 48-byte record per instrument, then 40-byte events to the end of the file. `Writer` in `nq/mbo_file.hpp` produces these files.
