# NQBook User Manual

This manual is for people and automated harnesses that run the `nqbook` binary and write plugins for it with NQCPPSDK. It describes the command line, the file formats, and the plugin API.

## 1. What you have

| Item | What it is |
|---|---|
| `nqbook` | the executable. It builds order books from MBO data, runs feature plugins, and writes feature rows |
| `plugins/adapters/replay.so` | an adapter plugin that replays an `.mbo` file |
| `plugins/adapters/synthetic.so` | an adapter plugin that generates test events |
| `plugins/features/example_features.so` | a feature plugin with spread, mid, and volume by side |
| NQCPPSDK | header-only C++ headers under `nq/`, plus a CMake function, for writing your own plugins. Source: `https://github.com/Zhou-London/nqcppsdk` |

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
    {"name": "example_features", "path": "...", "sdk_version": "0.1.0", "requires": "MBO", "ncol": 5, "params": {}}
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
nqbook run --adapter plugins/adapters/replay.so --adapter-param path=day.mbo \
           --plugin-dir plugins/features \
           --sampler 1s=time:1s --sampler 5s=time:5s --sampler t=trades:1 \
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
nqbook check-feed --adapter plugins/adapters/replay.so --adapter-param path=day.mbo
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

Parameters of the bundled adapters:

| Adapter | Parameter | Meaning |
|---|---|---|
| `replay.so` | `path` | the `.mbo` file to replay. Required |
| `synthetic.so` | `count` | number of events to generate. Required |
| `synthetic.so` | `instruments` | number of instruments. Default 1 |
| `synthetic.so` | `endless` | `true` keeps generating until the run is stopped. Default `false` |

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

## 8. Writing a feature plugin

### Project setup

A plugin is its own CMake project. It fetches the SDK from GitHub at a pinned tag:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(nqcppsdk
    GIT_REPOSITORY https://github.com/Zhou-London/nqcppsdk.git
    GIT_TAG v0.1.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(nqcppsdk)

nq_add_plugin(my_feature my_feature.cpp)
```

```bash
cmake -S . -B build
cmake --build build -j
nqbook check --plugin build/my_feature.so
```

`nq_add_plugin` builds `my_feature.so`. `nqbook` names the plugin `my_feature` and its columns `my_feature.<column>`.

Use the SDK tag that matches your `nqbook`. A mismatch gives exit code 3 at load time.

### The feature class

```cpp
#include <cmath>

#include <nq/feature.hpp>

struct Imbalance : nq::FeatureBase {
    static constexpr auto kColumns = nq::MakeColumns({
        {"imb", "ratio", "bid qty / (bid + ask) at best"},
        {"adds", "count", "add events", nq::kCumulative},
    });

    explicit Imbalance(const nq::Params& p) : min_qty_(p.GetInt("min_qty", 0)) {}

    void OnEvent(const nq::Event& e, const nq::BookView&) noexcept {
        if (e.action == nq::Action::Add && e.qty >= min_qty_) ++adds_;
    }

    void OnControl(const nq::Control& c) noexcept {
        if (c.type == nq::ControlType::Reset) adds_ = 0;
    }

    void Read(std::span<double> out, const nq::BookView& book) const noexcept {
        const auto b = book.Best(nq::Side::Bid);
        const auto a = book.Best(nq::Side::Ask);
        out[0] = (b && a) ? double(b->qty) / double(b->qty + a->qty) : NAN;
        out[1] = double(adds_);
    }

private:
    std::int64_t min_qty_;
    std::int64_t adds_ = 0;
};

NQ_PLUGIN(Imbalance)
```

A feature is a class with these members:

| Member | Role |
|---|---|
| `kColumns` | the output columns: name, unit, description, kind. `nq::MakeColumns` builds the array from an initializer list |
| constructor `(const nq::Params&)` | receives the parameters. Throw to reject them. The plugin then fails to load |
| `OnEvent(const Event&, const BookView&) noexcept` | called once per event. Optional. `FeatureBase` provides an empty one |
| `OnControl(const Control&) noexcept` | receives control events. `Reset` means the book was cleared. Optional |
| `Read(std::span<double>, const BookView&) const noexcept` | writes `kColumns.size()` values at sampling time |

`NQ_PLUGIN(A, B, C)` packs one or more feature classes into one plugin. The columns of `A` come first, then `B`, then `C`.

`nqbook` creates one instance of each feature class per instrument. An instance never sees events of another instrument.

### Column kinds

| Kind | Meaning | What `nqbook` writes |
|---|---|---|
| `nq::kSnapshot` (default) | the value at the sampling instant | the value as is |
| `nq::kCumulative` | a running total that never decreases | the increase since the previous row |

A cumulative column lets one feature serve several sampling rates. A 1-second sampler and a 1-minute sampler read the same total. Each takes its own difference.

### Three rules

- `OnEvent` does O(1) accumulation only. Reading the book and computing ratios belong in `Read`. `Read` runs only at sampling time.
- No wall clock, no random numbers, no I/O. Two runs over the same data must produce identical output.
- A `kCumulative` column resets to zero on `OnControl(Reset)`.

### Event

| Field | Meaning |
|---|---|
| `ts` | exchange event time, UTC nanoseconds |
| `id` | the order id. On a trade, the id of the order already in the book. 0 when unknown |
| `price` `qty` | fixed-point integers. The scale factors come from `Params` |
| `filled` | the quantity the book removed on a trade. 0 for other events |
| `side` | the side of the order. On a trade, the side of the order already in the book |
| `action` | `Add`, `Cancel`, `Modify`, or `Trade` |

`Clear` never arrives as an `Event`. It arrives as `OnControl(Reset)`.

### BookView

| Method | Returns |
|---|---|
| `Best(side)` | the best level as `std::optional<Level>`. `nullopt` when the side is empty |
| `GetLevel(side, k)` | the k-th level, k from 0. Costs O(k) |
| `Ts()` | the `ts` of the last event the book applied |

`Level` has `price`, `qty`, and `count`. A level is all orders at one price on one side.

### Params

| Method | Returns |
|---|---|
| `Symbol()` `TickSize()` `LotSize()` | the symbol, tick size, and lot size of the instrument this instance serves |
| `PriceScale()` `QtyScale()` | the fixed-point scale factors. `price / PriceScale()` is the floating-point price |
| `Get(key)` | `std::optional<std::string_view>` |
| `GetInt(key, fallback)` `GetDouble` `GetBool` | the parsed value, or the fallback when the key is absent |
| `Require(key)` `RequireInt` `RequireDouble` | the parsed value. Throws when the key is absent or malformed |

Parameters come from the plugin's `.params` file and from the command line.

### Testing a feature without nqbook

`nq/feature_testing.hpp` needs no engine:

```cpp
#include <nq/feature_testing.hpp>

nq::ParamsBuilder pb;
pb.PriceScale(100).QtyScale(10).Set("min_qty", "5");

nq::testing::Sandbox<Imbalance> sb(pb.Build());
sb.Book().SetBids({{10050, 30, 1}}).SetAsks({{10070, 10, 1}});   // {price, qty, count}, best level first
sb.Feed(nq::testing::Add(1, 1, nq::Side::Bid, 10050, 7));        // ts, id, side, price, qty
auto v = sb.Read();   // v[0] == 0.75, v[1] == 1
```

`Feed` hands the event to the feature. It leaves the mock book unchanged.

## 9. Writing an adapter plugin

```cpp
#include <nq/adapter.hpp>

class Replay : public nq::AdapterBase {
public:
    static constexpr const char* kName = "replay";

    explicit Replay(const nq::Params& p) : reader_(std::string(p.Require("path"))) {
        for (const auto& r : reader_.Instruments())
            table_.push_back(nq::Instrument{r.id, r.symbol, r.tick_size, r.lot_size});
    }

    nq::Next Next(nq::Mbo& out) {
        nq_mbo raw;
        const int rc = reader_.Next(raw);
        if (rc == 1) { out = nq::Mbo(raw); return nq::Next::Event; }
        return rc == 0 ? nq::Next::End : nq::Next::Error;
    }

    const nq::Instrument* FindInstrument(std::uint32_t id) const noexcept {
        for (const auto& i : table_) if (i.id == id) return &i;
        return nullptr;
    }

private:
    nq::mbo_file::Reader reader_;
    std::vector<nq::Instrument> table_;
};

NQ_ADAPTER(Replay)
```

Build it with the same `nq_add_plugin` as a feature.

| Member | Role |
|---|---|
| `kName` | the adapter name |
| constructor `(const nq::Params&)` | opens the file or the connection. Throw to report failure |
| `Next(nq::Mbo&)` | produces one event. See the table below |
| `FindInstrument(id) const noexcept` | returns the instrument's metadata, or `nullptr` for an unknown id |

Return values of `Next`:

| Value | Meaning |
|---|---|
| `Next::Event` | `out` holds one event |
| `Next::Again` | no event yet. `nqbook` asks again soon. A live source returns this instead of blocking |
| `Next::End` | the stream ended. Later calls also return `End` |
| `Next::Error` | the stream failed and ended |

An adapter is synchronous. `nqbook` calls `Next` and `FindInstrument` from one thread, never at the same time. `Next` must return within a bounded time. Return `Again` when no data is available, so a stop request can take effect.

`nq::Mbo` has the fields `ts`, `id`, `price`, `qty`, `instrument`, `side`, and `action`.

### The event stream contract

`nqbook` trusts the adapter on these points. `nqbook check-feed` verifies the ones it can:

- `ts` never decreases.
- `instrument` is a `uint32_t` id the adapter assigns. `FindInstrument(id)` must answer before the first event with that id.
- `id` identifies one order within the stream.
- `Add`: `qty` is the quantity of the new order.
- `Cancel`: `qty` is the quantity to remove. A `qty` of 0 removes the whole order. A `qty` larger than the remaining quantity also removes the whole order.
- `Modify`: `price` and `qty` are the new values. A price change moves the order to the back of the queue. A quantity increase also moves it to the back.
- `Trade`: `id` is the order already in the book, `side` is that order's side, `qty` is the traded quantity. With `id` 0 `nqbook` finds the level by `price` and reduces orders from the front of the queue.
- `Clear`: empties the book of that instrument. Send it after a sequence gap or a resubscribe.

Prices and quantities are fixed-point integers:

```
stored price    = round(price    * NQ_PRICE_SCALE)   NQ_PRICE_SCALE = 10000000000
stored quantity = round(quantity * NQ_QTY_SCALE)     NQ_QTY_SCALE   = 100000000
```

Example: the price 100.5 is stored as 1005000000000.

### Testing an adapter without nqbook

```cpp
#include <nq/adapter_testing.hpp>

nq::ParamsBuilder pb;
pb.Set("path", "fixture.mbo");
nq::testing::AdapterSandbox<Replay> sb(pb.Build());
auto events = sb.Drain();            // pulls until End or Error
sb.LastStatus();                     // nq::Next::End
```

## 10. The .mbo file

`replay.so` reads this format. `nq/mbo_file.hpp` has a `Writer` that produces it and a `Reader` that parses it. The file describes itself and needs no companion file.

```
header            16 bytes: "NQMBO\0\0\0", uint32 version = 1, uint32 instrument count
instrument table  48 bytes each: uint32 id, char symbol[28], int64 tick_size, int64 lot_size
events            40 bytes each, to the end of the file:
                  int64 ts, uint64 id, int64 price, int64 qty, uint32 instrument, uint8 side, uint8 action
```

All integers are little-endian. `side` is 0 for bid and 1 for ask. `action` is 0 add, 1 cancel, 2 modify, 3 trade, 4 clear.

To convert your own data, write a small program that uses `Writer`:

```cpp
#include <nq/mbo_file.hpp>

nq::mbo_file::Writer w("day.mbo", {nq::mbo_file::Writer::MakeInstrument(1, "BTC-USD", 10, 1)});
w.Write(nq_mbo{ts, id, price, qty, 1, NQ_BID, NQ_ADD});
w.Close();
```

## 11. Checklist for a harness

1. Build the plugin against the SDK tag that matches `nqbook`.
2. `nqbook check --plugin build/x.so`: exit code 0 means the plugin loads and its constructor accepts the parameters. Parse stdout with `--format json` for the column list.
3. `nqbook run ... --out-dir DIR`: exit code 0 means every row was written. Read `DIR/<sampler>.parquet` and `DIR/instruments.parquet`.
4. On any other exit code, read the single `nqbook: <message>` line on stderr. Codes 2, 3, and 4 point at the plugin; 5 and 6 at the command line; 7 at the adapter or the data; 8 at the output directory.
