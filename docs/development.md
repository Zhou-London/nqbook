# NQBook Development

This document is for people who change NQBook itself. Users of the binary and plugin authors read [manual.md](manual.md) instead.

## Source layout

```
include/ src/
  Orderbook.h/.cpp              one instrument's book: pooled orders, price levels, O(1) cancel, trade reduction, violation counters
  Pipeline.h                    one instrument: an Orderbook plus its FeatureEngine
  PipelineTable.h/.cpp          instrument id -> Pipeline, event routing, feed statistics, sampler advance across instruments
  Session.h/.cpp                assembles AdapterEngine, WriterEngine, and PipelineTable; owns the run loop
  Adapter/AdapterEngine.h/.cpp  the adapter thread: calls the adapter, fills the event queue, caches instrument metadata
  Feature/FeatureCatalog.h/.cpp the loaded feature plugins, their columns, and the sampler definitions
  Feature/FeatureEngine.h/.cpp  one instrument's plugin instances and samplers
  Feature/OrderbookView.h/.cpp  presents an Orderbook to plugins as nq_book_view
  Feature/Sampler.h/.cpp        time, event, and trade triggers; cumulative differences; emits rows
  Feature/RowBuffer.h           rows kept in memory, used by CSV output and tests
  Writer/RowSink.h              FeatureRow and the sink interface a sampler emits into
  Writer/WriterEngine.h/.cpp    the writer thread: drains the row queue into a ParquetWriter
  Writer/ParquetWriter.h/.cpp   Arrow builders and one Parquet file per sampler
  Writer/CsvWriter.h/.cpp       CSV output from RowBuffers
  Config.h/.cpp                 parsing of durations, sampler specs, .params files, plugin directories
  Report.h/.cpp                 the check listing (text and JSON) and the feed report
  Defaults.h                    every tunable default in one place
  main.cpp                      CLI11 subcommands and exit codes
plugins/features/example/       the example feature plugin
plugins/adapters/synthetic/     the synthetic adapter
tests/                          one executable per layer; tests/support.hpp holds the shared helpers
shared/nlib/                    nlib, fetched by CMake; do not edit here
shared/nqcppsdk/                NQCPPSDK, fetched by CMake; do not edit here
docs/                           this file and the manual
```

## How an event moves

```
adapter thread        main thread                                  writer thread
AdapterEngine::Run    Session::Run                                 WriterEngine::Run
  adapter.Next()        PipelineTable::Consume                       queue.try_pop
  Discover(instrument)    Resolve(instrument)   (new -> Pipeline)      ParquetWriter::Write
  queue.try_push  ---->   Advance(ts) on every live FeatureEngine
                          Pipeline::Apply
                            Orderbook::Apply -> filled
                            FeatureEngine::OnEvent
                              plugin on_event x N
                              Sampler::OnEvent -> Emit? -> RowSink::Push ---->
```

- The queue between the adapter thread and the main thread is `nlib::single_queue<nq_mbo>`. The queue between the main thread and the writer thread is `nlib::single_queue<FeatureRow>`. Both are single-producer single-consumer and never grow. A full queue makes the producer wait and counts a stall.
- Time samplers close bars before the event that crosses the boundary is applied. `PipelineTable::Apply` advances every live instrument on every event, then applies the event to its own instrument.
- `AdapterEngine` calls the adapter's `instrument()` on the adapter thread when it first sees an id, and keeps the answer in a map under a mutex. The main thread reads that map when it creates a `Pipeline`. The adapter itself is never called from two threads.
- Stopping: `Session::Run` sees the stop flag, `AdapterEngine::Stop` requests the adapter thread to stop and joins it, the table closes open bars, `WriterEngine::Finish` drains the row queue, writes `instruments.parquet`, and closes the files.

## Build

Requirements: CMake 3.28, a C++23 compiler (Apple clang 21 or GCC 14), Apache Arrow with Parquet, and GitHub access on the first configure.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

`build/compile_commands.json` is produced for clangd.

### Dependencies

| Dependency | How it arrives | Pin |
|---|---|---|
| nlib | `FetchContent`, checked out to `shared/nlib` | `GIT_TAG` in `CMakeLists.txt`, an annotated tag of `https://github.com/Zhou-London/nlib` |
| NQCPPSDK | `FetchContent`, checked out to `shared/nqcppsdk` | `GIT_TAG` in `CMakeLists.txt`, an annotated tag of `https://github.com/Zhou-London/nqcppsdk` |
| CLI11 | `FetchContent` into `build/_deps` | `v2.5.0` |
| Arrow, Parquet | `find_package` from the system | whatever is installed; 26.0.0 at the time of writing |

`shared/nlib` and `shared/nqcppsdk` are CMake's checkouts and are gitignored. Never edit them in place. Edit the standalone repositories, tag, push, and bump the `GIT_TAG`.

To build against a local checkout before a tag exists:

```bash
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_NQCPPSDK=/path/to/nqcppsdk
```

### Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
      -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j && ctest --test-dir build-asan
```

Replace `address,undefined` with `thread` for ThreadSanitizer. Both suites are expected to pass before a change lands; the adapter and writer threads are the parts TSan guards.

## Tests

| Executable | Covers |
|---|---|
| `orderbook_test` | the book: adds, cancels, modifies, trades by id and by level, clear, pool growth, the queue |
| `engine_test` | `OrderbookView`, one instrument through `Pipeline` with every sampler trigger, bar rules, close |
| `pipeline_table_test` | routing across instruments, unknown instruments, slot growth, `Session` end to end with the synthetic adapter, interruption |
| `adapter_engine_test` | the adapter thread: backpressure, instrument cache, stop |
| `writer_test` | `ParquetWriter` read back through Arrow, row groups, errors; `WriterEngine` across threads |
| `cli_test` | spawns `nqbook` for every subcommand, option, output format, and exit code |

`tests/support.hpp` holds `CHECK`, the fixed-point helpers, the event builders, and `RowIs`. Every test file includes it; do not define local copies.

The CLI test expects a plugin compiled with a wrong ABI hash. `tests/cli/fake_plugin.c` is that plugin, written in plain C against `feature_abi.h` only, so it also proves the ABI compiles as C.

## Changing things

### Release NQCPPSDK and pick it up

1. Edit and commit in the standalone SDK repository.
2. Bump `NQ_SDK_VERSION` in `nq/abi.h` and `VERSION` in the SDK's `CMakeLists.txt`.
3. `git tag -a vX.Y.Z -m "nqcppsdk vX.Y.Z"`, then push `main` and the tag.
4. Set `GIT_TAG vX.Y.Z` in this repository's `CMakeLists.txt`, delete `build/` and `shared/nqcppsdk`, reconfigure.
5. Update the `sdk_version` shown in the JSON example in `docs/manual.md`.

The engine checks the plugin's ABI hash at load time. The hash covers `abi.h` plus `feature_abi.h` for features and `abi.h` plus `adapter_abi.h` for adapters, so a change to one ABI header invalidates only that kind of plugin.

### Add a sampler trigger

`Trigger` in `Feature/Sampler.h`, `Sampler::OnEvent` and `Sampler::Close` in `Sampler.h/.cpp`, `ParseSamplerSpec` in `Config.cpp`, `TriggerName` in `Report.cpp`, the samplers table in `docs/manual.md`, and a case in `engine_test.cpp`.

### Add a feed report field

The counter in `Orderbook::Stats`, where the book increments it, the sum and the print in `PrintFeedReport` in `Report.cpp`, and the field table in `docs/manual.md`.

### Add a command-line option

An `Options` struct and an `Add*Options` function in `main.cpp`, a case in `cli_test.cpp`, and the option table in `docs/manual.md`.

## Conventions

- Engine code: PascalCase types and methods, `snake_case_` members, 4-space indent. nlib keeps the standard library's `snake_case`.
- No comments in code. The structure and the names carry the meaning; this file and the manual carry the rest.
- One header per class; a `.cpp` for anything that is not a template or a one-liner.
- Every tunable default lives in `Defaults.h`.
- Errors cross the CLI boundary as exceptions from the SDK's `plugin_error.hpp` or `std::invalid_argument`; `main.cpp` maps them to exit codes in one function.
- Every behavior change comes with a test in the executable that covers that layer, and with the manual updated in the same change.

## Performance baseline

Measured on an Apple M-series laptop with 5 million events over 50 instruments: 55% adds, 35% cancels, 7% trades, 3% modifies.

| Run | Time | Throughput |
|---|---|---|
| `check-feed` (books only) | 1.70 s | 2.9 M events/s |
| `run` with the example plugin, a 1-second sampler, and an every-100-events sampler, no output | 1.88 s | 2.7 M events/s |
| the same with Parquet output | 2.20 s | 2.3 M events/s |

Where the main thread spends its time, from a one-second sample of the second run:

| Share | Location |
|---|---|
| 24% | `Orderbook::FindOrCreateLevel`, the `std::map` of price levels |
| 42% | advancing the samplers of every live instrument on every event |
| 12% | `Orderbook::RemoveLevel`, erasing an emptied level from the map |
| 1% | plugin calls and sampling |

Copies of the 40-byte event along the path are not visible in the profile.

## Known gaps

- Sampler advance is O(instruments) per event. The fix is one global next-boundary per sampler template, with a sweep only when the boundary is crossed, plus an unconditional advance of the event's own instrument so a new instrument still starts on its first event.
- Price level lookup is a `std::map` walk. A hash map in front of it would turn most adds into one probe.
- The adapter thread and the main thread spin with `yield` when their queue is full or empty. On a slow consumer the adapter thread burns a core.
- `kRequires` from feature plugins and `kProvides` from adapters are recorded but not compared.
- `check-feed` does not report crossed books (best bid above best ask), which a correct MBO feed never produces.
