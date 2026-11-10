<p align="center"><img src="https://capsule-render.vercel.app/api?type=pulse&height=240&color=gradient&text=NQBook&fontSize=64&fontAlign=50&fontAlignY=42&desc=v0.1.3&descSize=22&descAlign=50&descAlignY=62" /></p>

<p align="center">
  <img alt="Version 0.1.3" src="https://img.shields.io/badge/version-0.1.3-blue" />
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Apache Arrow / Parquet" src="https://img.shields.io/badge/output-Parquet-50ABF1?logo=apache&logoColor=white" />
  <img alt="Plugins: dlopen" src="https://img.shields.io/badge/plugins-dlopen-orange" />
</p>

# NQBook

NQBook builds one order book per instrument from an MBO event stream. Feature plugins turn the books into rows, and samplers write the rows to Parquet.

## Build and run

Requires CMake 3.28, a C++23 compiler, and Apache Arrow with Parquet.

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
build/nqbook run --adapter build/plugins/adapters/synthetic.so \
                 --adapter-param count=100000 --adapter-param instruments=3 \
                 --plugin-dir build/plugins/features \
                 --sampler e=events:1000
```

`run` writes into `out/` under the current directory. `--out-dir` picks another directory. `nqbook help` lists the subcommands: `check`, `run`, and `check-feed`.

## Architecture

```
adapter thread         main thread                          writer thread
AdapterEngine          Session::Run                         WriterEngine
  adapter.Next()  ──►    PipelineTable                        ParquetWriter
                           per instrument: Pipeline
                             Orderbook
                             FeatureEngine
                               plugins ─► Samplers  ──────►
```

- Two single-producer single-consumer queues connect the three threads. A full queue makes the producer wait.
- `AdapterEngine` runs the adapter plugin on its own thread and caches instrument metadata.
- `PipelineTable` maps each instrument id to a `Pipeline`, which holds an `Orderbook` and a `FeatureEngine`.
- `Orderbook` keeps pooled orders in price levels and reduces resting orders on trades.
- `FeatureEngine` calls the feature plugins on each event and passes their values to the samplers.
- `WriterEngine` writes one Parquet file per sampler, plus `instruments.parquet`.

Sampling has three layers. Each layer uses only the layer below it.

| Layer | Class | Job |
|---|---|---|
| clock | `Clock/TimerQueue` | Wakes a key when the event clock reaches its due time. Knows nothing about instruments or samplers |
| trigger | `Feature/Trigger`: `BarTrigger`, `CountTrigger` | Decides when a row is due and calls `emit(ts)`. Pure logic: no plugins, no output |
| row | `Feature/Sampler` | Reads feature values, turns cumulative columns into deltas, and pushes the row to the sink |

`PipelineTable` joins the layers. On each event it fires the due timers, applies the event to its own instrument, and schedules that instrument's next due time. An idle instrument costs nothing per event.

| Path | Contents |
|---|---|
| `src/`, `include/` | the engine |
| `plugins/` | the example feature plugin and the synthetic adapter |
| `tests/` | one test executable per layer |
| `shared/` | nlib and [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk), fetched by CMake at the `GIT_TAG` pins |
