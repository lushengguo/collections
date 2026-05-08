# collections

This workspace collects several independent C++ projects around exchange infrastructure, networking, and parsing. Each project keeps its own local build, and the workspace root now also provides a superbuild `CMakeLists.txt` that can configure and build the whole set from one entry point.

Most projects include:
- a self-contained CMake build
- implemented source, header, test, and benchmark files stored directly in the project root
- GoogleTest-based validation or equivalent local tests
- benchmark or smoke-performance coverage where it makes sense
- a project README with scope, trade-offs, and local run instructions

## Current Contents

### Core Exchange

- [core-exchange/orderbook-matching-engine](core-exchange/orderbook-matching-engine)
- [core-exchange/pre-trade-risk-engine](core-exchange/pre-trade-risk-engine)
- [core-exchange/account-clearing-system](core-exchange/account-clearing-system)
- [core-exchange/unified-access-gateway](core-exchange/unified-access-gateway)

### Infrastructure

- [infrastructure/lock-free-structures](infrastructure/lock-free-structures)
- [infrastructure/memory-pool](infrastructure/memory-pool)
- [infrastructure/distributed-consistency](infrastructure/distributed-consistency)
- [infrastructure/market-data-push-system](infrastructure/market-data-push-system)

### Tooling

- [tooling/performance-toolkit](tooling/performance-toolkit)
- [tooling/observability-stack](tooling/observability-stack)

### Integration

- [integration/exchange-pipeline](integration/exchange-pipeline)

### Additional Repositories In The Workspace

- [reactor](reactor)
- [regex](regex)

`reactor` is currently Linux-oriented and depends on `epoll`, so the workspace superbuild skips it by default on macOS. You can still configure it directly, or enable it from the root superbuild on a compatible platform.

## Toolchain

- CMake 3.25+
- clang++ with C++20 support
- Ninja or Make

## Workspace Superbuild

The repository root now contains [CMakeLists.txt](CMakeLists.txt), which acts as a superbuild orchestrator. It does not merge all targets into one CMake project graph; instead it configures each child project in its own build directory and exposes aggregate root targets.

Configure the whole workspace:

```bash
cmake -S . -B build/superbuild -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
```

Build everything currently enabled:

```bash
cmake --build build/superbuild --target collections_all
```

Run each child project's tests through the superbuild:

```bash
cmake --build build/superbuild --target collections_test_all
```

Optional switches:

- `-DCOLLECTIONS_ENABLE_REACTOR=ON`
- `-DCOLLECTIONS_ENABLE_REGEX=OFF`

On macOS, `COLLECTIONS_ENABLE_REACTOR` defaults to `OFF` because the current reactor codebase uses Linux-specific networking APIs.

## Per-Project Build Flow

You can still build any project directly from its own directory. For example, configure a module in Debug for development and sanitizers:

```bash
cmake -S core-exchange/orderbook-matching-engine -B build/core-exchange/orderbook-matching-engine/debug -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core-exchange/orderbook-matching-engine/debug
ctest --test-dir build/core-exchange/orderbook-matching-engine/debug --output-on-failure
```

Configure a module in Release for benchmark runs:

```bash
cmake -S core-exchange/orderbook-matching-engine -B build/core-exchange/orderbook-matching-engine/release -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-exchange/orderbook-matching-engine/release --target run_benchmarks
```

Each project README explains the implemented scope, infrastructure reuse, validation coverage, and local build flow.

Measured benchmark results and design trade-offs are documented in [BENCHMARK_RESULTS.md](BENCHMARK_RESULTS.md).

