<p align="center"><img src="https://capsule-render.vercel.app/api?type=pulse&height=240&color=gradient&text=NQBook&fontSize=64&fontAlign=50&fontAlignY=42&desc=v0.1.1&descSize=22&descAlign=50&descAlignY=62" /></p>

<p align="center">
  <img alt="Version 0.1.1" src="https://img.shields.io/badge/version-0.1.1-blue" />
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white" />
  <img alt="CMake 3.28+" src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake&logoColor=white" />
  <img alt="Apache Arrow / Parquet" src="https://img.shields.io/badge/output-Parquet-50ABF1?logo=apache&logoColor=white" />
  <img alt="Plugins: dlopen" src="https://img.shields.io/badge/plugins-dlopen-orange" />
</p>

# NQBook

NQBook is an order book engine for MBO (market by order) data. It keeps one order book per instrument from a single market-wide event stream, reduces resting orders on trades without simulating a match, and turns the books into feature rows through plugins.

- **Plugins, not rebuilds.** Data sources (adapters) and feature computations are `.so` files loaded at startup. NQBook never has to be recompiled to add one.
- **Agent-safe plugin SDK.** Plugins see a small C ABI and a header-only C++ layer, [NQCPPSDK](https://github.com/Zhou-London/nqcppsdk). They never see engine code, and an SDK mismatch is refused at load time.
- **Streaming features.** Every feature is incremental. Samplers read them on time bars, event counts, or trade counts, and cumulative columns serve every sampling rate from one state.
- **Parquet out.** Rows stream to one Parquet file per sampler while the run proceeds, with a UTC nanosecond timestamp column and an instrument table.
- **Feed validation.** `check-feed` replays any adapter through bare books and reports timestamp regressions, unknown orders, and over-filled trades.

## Thirty seconds

```bash
cmake -S . -B build && cmake --build build -j
build/nqbook run --adapter build/plugins/adapters/synthetic.so \
                 --adapter-param count=100000 --adapter-param instruments=3 \
                 --plugin-dir build/plugins/features \
                 --sampler e=events:1000 \
                 --out-dir out
```

The report prints to stderr. `out/e.parquet` holds the feature rows and `out/instruments.parquet` the instrument table.

## Documents

| Document | For | Contents |
|---|---|---|
| [docs/manual.md](docs/manual.md) | users and harnesses | getting started, every command and option, the semantics of bars, output, and exit codes |
| [docs/development.md](docs/development.md) | people changing NQBook | source layout, threads and queues, build and test, dependency pins, release loop, performance baseline |
| [NQCPPSDK manual](https://github.com/Zhou-London/nqcppsdk/blob/main/docs/manual.md) | plugin authors | how to write, build, and test a feature or adapter plugin, and the API reference |

## Requirements

CMake 3.28, a C++23 compiler, Apache Arrow with Parquet (`brew install apache-arrow` on macOS, `libarrow-dev libparquet-dev` on Debian and Ubuntu), and GitHub access on the first configure.
