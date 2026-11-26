# AGENTS

> When an agent adds or changes code structure, features, or architectural boundaries in this scope, it MUST update this guide in the same change.

This file provides guidance for AI agents when working with the static-core runtime tooling module.

## Project Overview

This module contains the plugin-agnostic runtime side of ArkVM debugging, profiling, heap snapshots, code coverage, and stack analysis. It is shared infrastructure: language-specific behavior belongs in the appropriate plugin, not in this directory.

## Current Directory Map

```
runtime/tooling/
├── backtrace/                 # Native stack/backtrace extraction and symbolization
├── coverage/
│   └── coverage.py           # Line/method/branch coverage report generator
├── evaluation/
│   └── expression_loader.*   # Loads debugger expression bytecode produced by the frontend
├── hprof/
│   └── heap_dump.*           # Runtime heap dump support
├── inspector/
│   ├── connection/          # ASIO or OHOS WebSocket transport, selected at build time
│   ├── debugger/            # Breakpoints, thread state, object repository, debug info cache
│   ├── evaluation/          # Per-thread expression evaluation engine
│   ├── hprof/               # Heap snapshot serialization for CDP
│   ├── json_serialization/  # JSON-RPC/CDP serialization and errors
│   ├── types/               # Protocol value and response types
│   ├── tests/               # Inspector unit tests
│   ├── inspector_protocol.json
│   ├── inspector.*          # PtHooks coordinator and debugger session lifecycle
│   ├── inspector_server.*   # CDP message registration and responses
│   └── debugger_arkapi.*    # Native API used by the external debugger/toolchain
├── sampler/                  # SIGPROF sampling, .aspt writer/reader, profile model
├── coverage_listener.*       # Runtime bytecode coverage events
├── debugger.*                # DebugInterface and runtime instrumentation API
├── docs/debugger-attach.md   # Late attach and interpreter dispatch switch design
├── perf_counter.*            # Optional scoped performance counters
├── pt_*                      # PtHooks, language extension, thread and managed-code helpers
└── tools.*                   # Runtime tooling facade, including AttachDebugSession
```

## Architecture

### Inspector and CDP

- `Inspector` implements `PtHooks` and owns the debugger session, debuggable threads, and protocol server.
- `InspectorServer` registers CDP handlers for debugging, runtime inspection, target management, CPU sampling, and heap snapshots, then serializes responses with `types/` and `json_serialization/`.
- `inspector_protocol.json` is the protocol definition used by this server.
- The transport backend is fixed at build time: ASIO (`PANDA_TOOLING_ASIO=ON`) or the OHOS WebSocket backend.
- Breakpoints support URL/regex locations and conditional breakpoints. Conditional breakpoints evaluate frontend-generated base64 bytecode on the target application thread.
- `EvaluationEngine` evaluates that bytecode against a selected call frame; it does not parse or compile source expressions itself.
- `debugger_arkapi.cpp/h` bridges runtime operations used by the toolchain, including `NotifyDebugMode`, debugger startup/stop, CPU sampling control, and the shared sampling result buffer.
- `ObjectRepository` must be accessed from its application thread under the required mutator/handle scope rules. A temporary repository is also created on the application thread while running to convert `OnConsoleCall` arguments; the pause-only lifetime applies specifically to repositories used to answer breakpoint/frame queries.

### Profiling and Heap Snapshots

- `Sampler` uses a sampler thread and a listener thread. It can write `.aspt` files with `FileStreamWriter` or accumulate a CDP profile model through `InspectorStreamWriter`.
- `SamplesRecord` converts collected modules and samples into per-thread `ProfileInfo`, including dynamic frames, static frames, and the main-thread idle profile.
- `runtime/tooling/hprof/heap_dump.*` provides runtime heap dump support; `inspector/hprof/heap_snapshot_serializer.*` serializes a heap snapshot for the CDP heap snapshot request.
- Starting and stopping CPU sampling and heap snapshot behavior should preserve empty profiles and all existing progress/chunk events; do not remove logs or error paths merely to make tests pass.

### Late Debugger Attach

- `Tools::AttachDebugSession()` is the runtime entry for switching a running process into debug mode.
- It is guarded by `--debugger-attach-allowed`, rejects JIT-enabled runtimes, enables runtime debug mode, and deoptimizes AOT-backed methods. Within a `ScopedStopTheWorld` scope it switches managed threads to the debug dispatch table and collects compiled methods; `InvalidateCompiledEntryPoint` runs after leaving that scope.
- `docs/debugger-attach.md` is the authoritative design note. Changes to interpreter dispatch, AOT deoptimization, or safepoints must be validated with `debug_session_attach_test`.

### Coverage

- Runtime options `--code-coverage-enabled` and `--code-coverage-output` control collection.
- `coverage_listener.cpp/h` writes method ID, bytecode offset, and execution count into CSV; line and branch coverage are derived later by the report tool from disassembly and AST.
- `coverage/coverage.py` combines source, AST, disassembled `.pa`, and CSV data into `.info` and `genhtml` reports.
- The complete workflow and option caveats are documented in `static_core/docs/tooling/coverage.md`.

## Source-of-Truth Documents

- `docs/debugger-attach.md` - late attach and debug dispatch switching
- `../../docs/inspector.md` - inspector threading, suspension, breakpoints, and object model
- `../../docs/arkts-debugger-expression-evaluation.md` - expression bytecode contract
- `../../docs/tooling/coverage.md` - coverage workflow and report format
- `sampler/docs/` - sampling design, CPU profile output and usage
- `perf_counter.md` - performance counter build and usage

## Build Commands

### OHOS full build

Run from the OpenHarmony source root:

```bash
./build.sh --product-name rk3568 --build-target libarkruntime
```

### Standalone CMake build

From `runtime_core/static_core`, set `HOST_TOOLCHAIN_FILE` to the host toolchain file used by your standalone build under `cmake/toolchain/`, then run:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${HOST_TOOLCHAIN_FILE:?set the host toolchain file}" \
  -GNinja .
cmake --build build --target arkinspector
```

## Testing

From an existing standalone build directory, the plain targets build the test binaries and the `_gtests` targets execute them. The attach suite is always available; from `static_core`, run:

```bash
ninja -C build debug_session_attach_test_gtests
```

The inspector suite is intentionally disabled under ASan (`PANDA_ENABLE_ADDRESS_SANITIZER`), so run it separately only in non-ASan builds:

```bash
ninja -C build arkinspector_tests_gtests
```

The CPU sampling suite is not supported on ARM64 (`PANDA_TARGET_ARM64`) and must be run separately on supported configurations:

```bash
sampling_suite=$(ninja -C build -t targets all | sed -n 's/^\(sampling_[[:alnum:]_]*_test_gtests\):.*/\1/p')
test -n "$sampling_suite" && ninja -C build "$sampling_suite"
```

Additional relevant runtime targets for cross-component changes are `arkruntime_heap_dump_test`, `arkruntime_async_stack_test`, and `arkruntime_async_stack_transport_test`.

For debugger protocol changes, add or update focused tests in `inspector/tests/`. For interpreter/AOT attach changes, extend `runtime/tests/tooling/debug_session_attach/`. For sampler/profile model changes, extend `runtime/tests/tooling/sampler/`.

## Change Rules

- Do not move language-specific debugger semantics into plugin-agnostic code.
- Preserve `CheckVmDead`, application-thread ownership, mutator lock/handle scope, and pause-state guards.
- A connection backend change must be compiled for both ASIO and OHOS backends.
- A CPU sampling change must cover both file output and `InspectorStreamWriter`/CDP output paths.
- New CDP methods must update `inspector_protocol.json`, `InspectorServer` registration, the runtime implementation, serialization types, and focused tests together.
