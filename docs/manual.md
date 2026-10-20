# NQBook Manual

This manual is for people and automated harnesses that run the `nqbook` binary. It covers getting started, every command and option, and the semantics NQBook guarantees. Writing plugins is covered by the [NQCPPSDK manual](https://github.com/Zhou-London/nqcppsdk/blob/main/docs/manual.md). People who change NQBook itself read [development.md](development.md).

## 1. What you have

| Item | What it is |
|---|---|
| `nqbook` | the executable. It builds order books from MBO data, runs feature plugins, and writes feature rows |
| `plugins/adapters/synthetic.so` | an adapter plugin that generates test events |
| `plugins/features/example_features.so` | a feature plugin with spread, mid, and volume by side |

`nqbook` needs the Apache Arrow and Parquet shared libraries at run time. On macOS: `brew install apache-arrow`. On Debian and Ubuntu: `libarrow` and `libparquet`.

## 2. Terms

| Term | Meaning |
|---|---|
| event | one MBO record: an add, cancel, modify, trade, or clear for one order |
| instrument | one tradable thing with its own order book, identified by a `uint32` id that the adapter assigns |
| adapter | a plugin that produces the event stream, from a file or a live feed |
| feature | a plugin that computes numbers from an order book |
| column | one number a feature produces, named `plugin.column` in the output |
| snapshot column | a column that holds the value at the sampling instant |
| cumulative column | a column that holds a running total; the output row holds the increase since the previous row |
| sampler | a rule that decides when to read the features and emit a row: every N seconds, every N events, or every N trades |
| bar | the time span between two rows of a time sampler; a bar closes at a whole multiple of its length in event time |
| event time | the `ts` field of the event, UTC nanoseconds; `nqbook` has no other clock |

## 3. First run

This command needs no data file. It generates 100000 events for 3 instruments, computes the example features, and writes one Parquet file per sampler into `out/`:

```bash
nqbook run --adapter plugins/adapters/synthetic.so \
           --adapter-param count=100000 --adapter-param instruments=3 \
           --plugin-dir plugins/features \
           --sampler e=events:1000 \
           --out-dir out
```

The report goes to stderr. The exit code is 0. The files are `out/e.parquet` and `out/instruments.parquet`.

## 4. Commands

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
nqbook check --plugin-dir plugins/features --sampler 1s=time:1s --sampler t=trades:1
nqbook check --plugin plugins/features/example_features.so --format json
```

`check` loads every plugin and initializes it once with a placeholder instrument. A plugin whose constructor throws fails here. The output lists plugins, columns, and samplers. `--format json` prints the same list as JSON:

```json
{
  "plugins": [
    {"name": "example_features", "path": "...", "sdk_version": "0.1.2", "requires": "MBO", "ncol": 5, "params": {}}
  ],
  "columns": [
    {"name": "example_features.spread", "unit": "price", "kind": "snapshot", "doc": "best ask minus best bid", "plugin": "example_features"}
  ],
  "samplers": [
    {"name": "1s", "trigger": "time", "every": 1000000000, "fill_gaps": false, "columns": ["example_features.spread", "..."]}
  ]
}
```

### run

```bash
nqbook run --adapter plugins/adapters/synthetic.so --adapter-param count=100000 \
           --plugin-dir plugins/features \
           --sampler e=events:1000 --sampler t=trades:1 \
           --out-dir out
```

`run` reads events from the adapter, applies them to the books, and lets the samplers emit rows. With `--out-dir` it writes the rows to disk while the run proceeds. At the end it prints the feed report, writes `instruments.parquet`, and closes the files. Without `--out-dir` it prints only the report.

| Option | Meaning |
|---|---|
| `--out-dir DIR` | directory that receives one file per sampler plus `instruments.parquet` |
| `--out-format parquet\|csv` | `parquet` writes rows to disk during the run. `csv` keeps rows in memory and writes `<sampler>.csv` at the end. Default `parquet` |
| `--row-group N` | rows per Parquet row group. Default 65536 |

Ctrl-C or SIGTERM stops a run cleanly. The bars that have not closed yet are emitted, the output files are written, and the exit code is 130.

### check-feed

```bash
nqbook check-feed --adapter plugins/adapters/synthetic.so --adapter-param count=100000
```

`check-feed` validates an adapter. The report has one `feed` line for the whole market, one `book` line that sums all books, and one line per instrument:

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
| `writer rows=… stalls=…` | `run` only. Rows written, and times the run waited because the writer was busy |

## 5. Options

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

Parameters of `synthetic.so`:

| Parameter | Meaning |
|---|---|
| `count` | number of events to generate. Required |
| `instruments` | number of instruments. Default 1 |
| `endless` | `true` keeps generating until the run is stopped. Default `false` |
| `fail_after` | ends the stream with an error after this many events. Default 0, never |

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

## 6. Output

One Parquet file per sampler, ZSTD compressed, with the Arrow schema stored in the file:

| Column | Type |
|---|---|
| `instrument` | `uint32`, the id the adapter assigned |
| `ts` | `timestamp[ns, tz=UTC]` |
| one column per feature column | `float64`, named `plugin.column` |

`instruments.parquet` maps `instrument` to `symbol`, `tick_size`, and `lot_size`. The id is meaningful only within one run. Align runs by `symbol`.

A snapshot column holds the value at the sampling instant. A cumulative column holds the increase since the previous row of the same sampler, so the 1-second rows sum to the 5-second rows. `check` prints the kind of every column.

With `--out-format csv` each sampler becomes `<sampler>.csv` with the columns `instrument,symbol,ts,...`. `ts` is UTC nanoseconds as an integer.

## 7. Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 2 | a plugin or adapter failed to load: `dlopen` failed or an entry symbol is missing |
| 3 | the plugin was built against a different SDK version. Rebuild the plugin with the SDK that matches this `nqbook` |
| 4 | a plugin or adapter `init` failed, usually a missing or invalid parameter |
| 5 | a sampler definition is invalid or duplicated |
| 6 | a `.params` file or `--adapter-param` is malformed |
| 7 | the adapter ended with an error status |
| 8 | opening or writing the output failed |
| 130 | interrupted by SIGINT or SIGTERM |
| 100 and above | command-line usage error |

Every error prints one line `nqbook: <message>` to stderr.

## 8. Plugins

Feature plugins and adapter plugins are `.so` files built with NQCPPSDK. The [NQCPPSDK manual](https://github.com/Zhou-London/nqcppsdk/blob/main/docs/manual.md) explains how to write, build, and test them, and states the contract an adapter's event stream must follow. Build a plugin against the SDK version that `nqbook check --format json` reports as `sdk_version`; a mismatch is refused with exit code 3.

## 9. Checklist for a harness

1. Build the plugin against the SDK version `nqbook` reports.
2. `nqbook check --plugin build/x.so`: exit code 0 means the plugin loads and its constructor accepts the parameters. Parse stdout with `--format json` for the column list.
3. `nqbook run ... --out-dir DIR`: exit code 0 means every row was written. Read `DIR/<sampler>.parquet` and `DIR/instruments.parquet`.
4. On any other exit code, read the single `nqbook: <message>` line on stderr. Codes 2, 3, and 4 point at the plugin; 5 and 6 at the command line; 7 at the adapter or the data; 8 at the output directory.
