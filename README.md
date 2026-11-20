<p align="center"><img src="https://capsule-render.vercel.app/api?type=pulse&height=240&color=gradient&text=NQ%20HPC&fontSize=64&fontAlign=50&fontAlignY=42&desc=v0.2.0&descSize=22&descAlign=50&descAlignY=62" /></p>

<p align="center">
  <img alt="Version 0.2.0" src="https://img.shields.io/badge/version-0.2.0-blue" />
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Apache Arrow / Parquet" src="https://img.shields.io/badge/output-Parquet-50ABF1?logo=apache&logoColor=white" />
  <img alt="Plugins: dlopen" src="https://img.shields.io/badge/plugins-dlopen-orange" />
</p>

# NQ HPC

NQ HPC turns a market data feed into feature rows. Feature plugins compute the values, and samplers write the rows to Parquet.

One run reads one feed. The adapter plugin decides which:

| Feed | Record | Per instrument |
|---|---|---|
| MBO | one order event | an order book and the feature plugins |
| OHLCV | one bar: open, high, low, close, volume | the feature plugins |

Every feature plugin must read the feed the adapter sends. A mismatch stops the run with exit code 9.

## Build and run

Requires CMake 3.28, a C++23 compiler, and Apache Arrow with Parquet.

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
build/nqhpc run --adapter build/plugins/adapters/synthetic.so \
                --adapter-param count=100000 --adapter-param instruments=3 \
                --plugin build/plugins/features/example_features.so \
                --sampler e=events:1000
build/nqhpc run --adapter build/plugins/adapters/synthetic_ohlcv.so \
                --adapter-param count=100000 --adapter-param instruments=3 \
                --plugin build/plugins/features/example_ohlcv_features.so \
                --sampler 5m=time:5m
```

The first command reads an MBO feed, the second an OHLCV feed.

`run` writes into `out/` under the current directory. `--out-dir` picks another directory. `nqhpc help` lists the subcommands: `check`, `run`, and `check-feed`.

Each sampler and cross table in Parquet has these columns, in order:

| Column | Type | Contents |
|---|---|---|
| `instrument` | uint32 | instrument id |
| `symbol` | string | instrument symbol |
| `ts` | int64 | row time in nanoseconds since the Unix epoch, UTC |
| `date` | date32 | UTC calendar day of `ts`, for partitioning |
| one per feature | double | feature value |

CSV output has the same columns without `date`.

## Architecture

```
adapter thread         main thread                          writer thread
AdapterEngine          Session::Run                         WriterEngine
  adapter.Next()  ──►    PipelineTable                        ParquetWriter
                           per instrument: Pipeline
                             Orderbook (MBO feed)
                             FeatureEngine
                               plugins ─► Samplers  ──────►
```

- Two single-producer single-consumer queues connect the three threads. A full queue makes the producer wait.
- `AdapterEngine` runs the adapter plugin on its own thread and caches instrument metadata.
- `AdapterEngine`, `PipelineTable`, `Pipeline`, and `Session` are templates over the record type: `nq_mbo` or `nq_ohlcv`.
- `PipelineTable` maps each instrument id to a `Pipeline`. An MBO `Pipeline` holds an `Orderbook` and a `FeatureEngine`. An OHLCV `Pipeline` holds a `FeatureEngine` only.
- `Orderbook` keeps pooled orders in price levels and reduces resting orders on trades.
- `FeatureEngine` calls the feature plugins on each record and passes their values to the samplers. It hands warm-up bars to the plugins only.
- `WriterEngine` writes one Parquet file per table (sampler or cross), plus `instruments.parquet`. Each row holds the instrument id and its symbol.

Sampling has three layers. Each layer uses only the layer below it.

| Layer | Class | Job |
|---|---|---|
| clock | `Clock/TimerWheel` | Wakes a key when the event clock reaches its due time. Holds the timers in a two-level wheel: 4096 slots of one millisecond, then 4096 slots of 4.096 seconds. Knows nothing about instruments or samplers |
| trigger | `Feature/Trigger`: `BarTrigger`, `CountTrigger` | Decides when a row is due and calls `emit(ts)`. Pure logic: no plugins, no output |
| row | `Feature/Sampler` | Reads feature values, turns cumulative columns into deltas, and pushes the row to the sink |

`PipelineTable` joins the layers. On each record it fires the due timers, applies the record to its own instrument, and schedules that instrument's next due time. An idle instrument costs nothing per record.

A cross computes columns across all instruments on a time grid. `--cross NAME=PLUGIN:EVERY` runs the cross that `PLUGIN` exports and writes one more table named `NAME`:

```bash
build/nqhpc run --adapter build/plugins/adapters/synthetic.so \
                --adapter-param count=100000 --adapter-param instruments=3 \
                --plugin build/plugins/features/example_cross_features.so \
                --cross x=example_cross_features:1us
```

| Class | Job |
|---|---|
| `Feature/CrossPanel` | Holds the input and output columns of one cross, one row per instrument, column-major and 64-byte aligned. Tracks the rows that had a record since the last boundary |
| `Feature/CrossStage` | Owns the clock of one cross. At a boundary it reads the changed instruments, calls the cross, and pushes one block of rows to the sink |

`PipelineTable` fires a cross before the samplers of the same instant, so a sampler row at a boundary sees the cross output of that boundary. A record costs one comparison and one bit per cross. `tests/cross_bench` measures the cost per boundary. A cross reads only snapshot columns.

`ta_ohlcv` exports a cross that ranks and z-scores its 8 snapshot features across instruments. Its cumulative features `ret`, `gap`, `rv` and `gk` get no cross-section; a feature that needs one needs a snapshot twin first. Each instrument row of the sampler table carries the result as `ta_ohlcv.<feature>_rank` and `ta_ohlcv.<feature>_z`. This run reads the lake data that `nqlake mmap` writes from `plugins/adapters/nqlake_ohlcv/mmap.sql`:

```bash
build/nqhpc run --adapter build/plugins/adapters/nqlake_ohlcv.so \
                --plugin build/plugins/features/ta_ohlcv.so \
                --sampler 1h=time:1h --cross xs=ta_ohlcv:1h
```

Warm-up fills feature state before the feed starts. An adapter may send OHLCV bars ahead of its first record, and `PipelineTable` hands each bar to the features of its instrument. A warm-up bar moves no clock and writes no row. Cumulative columns count from the end of warm-up. The feed report shows the number as `warmup_bars`.

`nqlake_ohlcv` sends the bars before `warmup_until` as warm-up bars. `ta_ohlcv` ignores warm-up bars. Its indicators start from the first feed record.

Both synthetic adapters take `warmup=N` to send `N` warm-up bars.

The members of a boundary are the instruments with a bar since the last boundary and a value for the feature. A feature with fewer than `cross_min` members, 20 by default, gets NaN rank and z. `cross_min` is set in `ta_ohlcv.params` beside the `.so`.

The `ts` of an OHLCV record is the start of its period. A `time` sampler with the same period writes one row per record, stamped at the end of the period. The `trades` trigger needs an MBO feed.

| Path | Contents |
|---|---|
| `src/`, `include/` | the engine |
| `plugins/` | one example feature plugin and one synthetic adapter per feed, the `example_cross_features` plugin with a cross, the `nqlake_ohlcv` adapter, the `ta_ohlcv` feature plugin with a cross |
| `tests/` | one test executable per layer |
| `shared/` | nlib and [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk), fetched by CMake at the `GIT_TAG` pins |
