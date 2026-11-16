<p align="center"><img src="https://capsule-render.vercel.app/api?type=pulse&height=240&color=gradient&text=NQ%20HPC&fontSize=64&fontAlign=50&fontAlignY=42&desc=v0.1.3&descSize=22&descAlign=50&descAlignY=62" /></p>

<p align="center">
  <img alt="Version 0.1.3" src="https://img.shields.io/badge/version-0.1.3-blue" />
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
- `FeatureEngine` calls the feature plugins on each record and passes their values to the samplers.
- `WriterEngine` writes one Parquet file per sampler, plus `instruments.parquet`. Each sampler row holds the instrument id and its symbol.

Sampling has three layers. Each layer uses only the layer below it.

| Layer | Class | Job |
|---|---|---|
| clock | `Clock/TimerWheel` | Wakes a key when the event clock reaches its due time. Holds the timers in a two-level wheel: 4096 slots of one millisecond, then 4096 slots of 4.096 seconds. Knows nothing about instruments or samplers |
| trigger | `Feature/Trigger`: `BarTrigger`, `CountTrigger` | Decides when a row is due and calls `emit(ts)`. Pure logic: no plugins, no output |
| row | `Feature/Sampler` | Reads feature values, turns cumulative columns into deltas, and pushes the row to the sink |

`PipelineTable` joins the layers. On each record it fires the due timers, applies the record to its own instrument, and schedules that instrument's next due time. An idle instrument costs nothing per record.

The `ts` of an OHLCV record is the start of its period. A `time` sampler with the same period writes one row per record, stamped at the end of the period. The `trades` trigger needs an MBO feed.

| Path | Contents |
|---|---|
| `src/`, `include/` | the engine |
| `plugins/` | one example feature plugin and one synthetic adapter per feed, the `nqlake_ohlcv` adapter, the `ta_ohlcv` feature plugin |
| `tests/` | one test executable per layer |
| `shared/` | nlib and [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk), fetched by CMake at the `GIT_TAG` pins |
