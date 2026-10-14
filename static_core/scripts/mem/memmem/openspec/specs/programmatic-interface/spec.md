# programmatic-interface Specification

## Purpose

Defines the public Python API for constructing and running benchmark flows programmatically.

## Requirements

### Requirement: Programmatic API runs validated flows
The public facade SHALL expose `execute_flow(flow: Flow, device: Device, dst: Path, *, reboot: bool = False, device_log: bool = True) -> Path` in place of `run`. It SHALL revalidate canonical flow and all backend command support before evidence reservation/device actions, execute exactly once, receive evidence, perform shared local structural validation, and return the lexically normalized absolute destination. It SHALL exclusively create the exact dst, never append a layout prefix, and reject existing destinations without modifying them. It SHALL NOT repeat execution, accept a smaps filter, generate reports, write parent flow provenance, configure/reset logging, or convert failures to statuses. Newly created partial evidence SHALL remain on failure. Removed `run`, `logs`, and `hilog` interfaces SHALL NOT be retained as compatibility shims. Public `configure_logger`, `get_logger`, and `reset_logger` helpers SHALL remain separately available.

#### Scenario: Execution succeeds
- **WHEN** a caller supplies canonical flow, a device and a new destination
- **THEN** execution returns that normalized absolute evidence path, whose required structure and declared files pass the local reader, with no view generated

#### Scenario: Flow was mutated after construction
- **WHEN** a supplied flow violates canonical invariants
- **THEN** execution raises before destination reservation or device actions

#### Scenario: Command is unsupported
- **WHEN** the device reports a canonical command unsupported
- **THEN** execution raises before reserving its evidence destination or preparing the device

#### Scenario: Destination exists
- **WHEN** dst already exists
- **THEN** execution fails without modifying it or performing device actions

#### Scenario: Library failure occurs
- **WHEN** capture, receive, cleanup or final structural validation fails
- **THEN** the call raises, retains new partial evidence, and preserves caller logger configuration

#### Scenario: Caller disables device logging
- **WHEN** `execute_flow` receives `device_log=False`
- **THEN** device log setup, stream handling and receive are disabled without affecting memmem application logging

#### Scenario: Runtime options are keyword-only
- **WHEN** `execute_flow` is called
- **THEN** reboot and device_log are keyword-only, while flow, device and dst follow the declared new signature

#### Scenario: Runtime option defaults match CLI
- **WHEN** `execute_flow` omits optional controls
- **THEN** reboot is False and device_log is True; repeats and filters are not execution options

#### Scenario: Programmatic destination resolves against the caller's working directory
- **WHEN** dst is relative
- **THEN** `execute_flow` anchors and lexically normalizes it at entry and returns its absolute form

#### Scenario: Repeated execution is caller-orchestrated
- **WHEN** a caller needs repeated execution
- **THEN** it invokes `execute_flow` once per new evidence destination or uses CLI repeats; `execute_flow` does not accept repeats

#### Scenario: Legacy execution keywords are removed
- **WHEN** logs, hilog, repeats, or smaps_filter is supplied to `execute_flow`
- **THEN** the unexpected keyword is rejected

#### Scenario: Programmatic memmem log level is configured
- **WHEN** a caller configures info logging before `execute_flow`
- **THEN** the API honors and preserves that logger configuration

#### Scenario: Programmatic memmem log file is configured
- **WHEN** a caller configures a log file before `execute_flow`
- **THEN** emitted memmem logs use that destination and the API does not reset it

### Requirement: Programmatic API constructs flows with thin wrappers
The facade SHALL preserve `flow`, `app_flow`, all existing canonical command builders, and `unprocessed_flow`, `unprocessed_app_flow`, `unprocessed_snapshot`, `unprocessed_screenshot`, and `repeat` builders as thin validated-model wrappers with no hidden defaults beyond their underlying models. Flow description SHALL remain optional. App-flow construction SHALL require label, bundle, ability, terminate and commands. Repeat construction SHALL require n_iter, iter_var and commands and accept empty commands under existing validation. Builders SHALL raise validation errors immediately. `app` SHALL remain absent from public exports. Canonical builder output SHALL be executable through `execute_flow`; macro-bearing output SHALL first pass through the unchanged public `preprocess_flow`.

#### Scenario: Flow wrapper accepts app models
- **WHEN** a caller invokes `lib.flow()` with a list of app models constructed by `lib.app_flow()`
- **THEN** the system returns a validated flow model

#### Scenario: Flow wrapper accepts description metadata
- **WHEN** a caller invokes `lib.flow()` with `desc` set to a string
- **THEN** the system returns a validated flow model containing that description metadata

#### Scenario: App-flow wrapper requires explicit fields
- **WHEN** a caller invokes `lib.app_flow()`
- **THEN** the caller MUST explicitly provide label, bundle, ability, terminate, and commands

#### Scenario: Command wrappers validate immediately
- **WHEN** a caller invokes a command wrapper with invalid labels, coordinates, velocity, step length, direction, key, or text
- **THEN** the wrapper raises validation failure during construction

#### Scenario: Text wrapper constructs a flat payload command
- **WHEN** a caller invokes `lib.text()` with a string
- **THEN** the wrapper returns a `TextCommand` whose payload is the text string directly

#### Scenario: Unprocessed flow wrapper accepts unprocessed app models
- **WHEN** a caller invokes `lib.unprocessed_flow()` with a list of unprocessed app models constructed by `lib.unprocessed_app_flow()`
- **THEN** the system returns a validated unprocessed flow model

#### Scenario: Unprocessed app-flow wrapper requires explicit fields
- **WHEN** a caller invokes `lib.unprocessed_app_flow()`
- **THEN** the caller MUST explicitly provide label, bundle, ability, terminate, and commands

#### Scenario: Unprocessed snapshot and screenshot wrappers construct lenient commands
- **WHEN** a caller invokes `lib.unprocessed_snapshot()` or `lib.unprocessed_screenshot()` with a label containing characters illegal in the canonical schema
- **THEN** the wrapper constructs the lenient unprocessed command without label validation

#### Scenario: Repeat wrapper requires explicit payload fields
- **WHEN** a caller invokes `lib.repeat()`
- **THEN** the caller MUST explicitly provide `n_iter`, `iter_var`, and `commands` (which may be empty), and the wrapper validates them immediately

#### Scenario: Unprocessed builder validation failures are surfaced
- **WHEN** a caller invokes an unprocessed builder wrapper with invalid `n_iter`, invalid `iter_var`, or invalid commands
- **THEN** the wrapper raises validation failure during construction

#### Scenario: Repetitive scenarios can be generated with Python
- **WHEN** a caller uses Python loops or helper functions to assemble repeated command sequences
- **THEN** the wrapper-produced models can be combined into a valid flow for `lib.execute_flow()`

#### Scenario: Old app wrapper is not public
- **WHEN** a caller imports documented public helpers from `lib.py`
- **THEN** `app_flow` is available and `app` is not part of the public facade

### Requirement: Programmatic API constructs multi-touch commands
The system SHALL provide `lib.multi_touch_input(finger_list: list[dict[str, int]], duration_ms: int)`. The wrapper SHALL construct and return the canonical validated `MultiTouchInputCommand` immediately, SHALL expose no schema class or dataclass in its parameters, and SHALL introduce no hidden defaults.

#### Scenario: Multi-touch wrapper constructs a canonical command
- **WHEN** a caller invokes `lib.multi_touch_input` with a valid two- or three-item finger list and duration
- **THEN** the wrapper returns a validated canonical `MultiTouchInputCommand`

#### Scenario: Multi-touch wrapper validates immediately
- **WHEN** a caller invokes `lib.multi_touch_input` with an invalid finger list or duration
- **THEN** the wrapper raises schema validation failure during construction

### Requirement: Programmatic API exports public type names
The facade SHALL continue exporting existing public flow, app-flow, command, Device, Hdc, Abd, unprocessed flow/app-flow, Macro, RepeatMacro, UnprocessedSnapshotCommand and UnprocessedScreenshotCommand types and `preprocess_flow` for typed scripts without `src` imports. It SHALL export `execute_flow` and `create_view` and SHALL NOT export `run`. Payload structs/aliases, UnprocessedCommand, generic model bases, storage/parsed objects, equivalence helpers, and internal runner options SHALL remain internal.

#### Scenario: Typed user script annotates flow helpers
- **WHEN** a user script annotates helper functions or variables with `lib.Flow`, `lib.AppFlow`, or `lib.Command`
- **THEN** mypy can resolve those names from `lib.py` without `src.*` imports

#### Scenario: Typed user script annotates device handles
- **WHEN** a user script annotates HDC, ABD, or device variables with public `lib.py` type names
- **THEN** mypy can resolve those names from `lib.py` without `src.*` imports

#### Scenario: Existing HDC type remains available
- **WHEN** a documented existing user script imports `lib.Hdc` and `lib.Device`
- **THEN** those public names remain available

#### Scenario: Precise command types are available
- **WHEN** a user script needs precise builder return annotations such as `lib.TapCommand` or `lib.WaitCommand`
- **THEN** those command model type names are available from the public `lib.py` facade

#### Scenario: Typed user script annotates unprocessed types
- **WHEN** a user script annotates preprocessing inputs with `lib.UnprocessedFlow`, `lib.UnprocessedAppFlow`, `lib.Macro`, `lib.RepeatMacro`, `lib.UnprocessedSnapshotCommand`, or `lib.UnprocessedScreenshotCommand`
- **THEN** mypy can resolve those names from `lib.py` without `src.*` imports

#### Scenario: Preprocessing function is available from the facade
- **WHEN** a user script calls or annotates `lib.preprocess_flow`
- **THEN** the function is available from the public `lib.py` facade without `src.*` imports

#### Scenario: Payload structs remain private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** payload struct names such as `TapPayload`, `RepeatMacroPayload`, and `WaitPayload` are not part of the public facade

#### Scenario: Unprocessed command union remains private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** the `UnprocessedCommand` union is not part of the public facade

#### Scenario: Internal runner options remain private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** internal orchestration types such as `BenchmarkOptions` are not part of the public facade

### Requirement: Programmatic API preprocesses unprocessed flows
The system SHALL provide a public `lib.preprocess_flow(flow: UnprocessedFlow) -> Flow` function as the only official transformation from an unprocessed flow to a canonical flow. The function SHALL revalidate its input through `UnprocessedFlow` before expanding repeat macros in place preserving command order, substitute `{<iter_var>}` with 0-based iteration numbers in the range `[0, n_iter-1]` only in repeated commands whose payload is itself a string (flat string payloads; payload structs are not traversed), preserve flow description metadata, perform no I/O and no device access, and raise `pydantic.ValidationError` for unprocessed schema errors and for post-expansion canonical validation errors. Input acceptance is enforced statically: the signature takes `UnprocessedFlow` only, so passing a canonical flow or a raw dict is a mypy error, while structurally invalid `UnprocessedFlow` instances fail the wrapper's revalidation at runtime.

#### Scenario: Preprocessing expands macros into canonical flow
- **WHEN** a caller invokes `lib.preprocess_flow()` with an unprocessed flow containing repeat macros
- **THEN** the function returns a canonical flow containing expanded commands with substituted labels in declared order

#### Scenario: Preprocessing preserves flow description
- **WHEN** a caller invokes `lib.preprocess_flow()` with an unprocessed flow containing `$desc` metadata
- **THEN** the returned canonical flow preserves the description metadata

#### Scenario: Preprocessing input is statically typed
- **WHEN** a caller passes a raw dict or a canonical flow to `lib.preprocess_flow()` instead of an `UnprocessedFlow`
- **THEN** mypy rejects the call at type-check time, and structurally invalid `UnprocessedFlow` instances fail the wrapper's revalidation with a validation error at runtime rather than being transformed

#### Scenario: Preprocessing performs no device access
- **WHEN** a caller invokes `lib.preprocess_flow()`
- **THEN** the function performs no I/O and no device operations and produces identical output for identical input

#### Scenario: Preprocessing raises on invalid input
- **WHEN** a caller invokes `lib.preprocess_flow()` with an unprocessed flow that fails unprocessed validation or macro body validation
- **THEN** the function raises a validation error before producing a canonical flow

### Requirement: Programmatic API creates device handles through public helpers
The system SHALL preserve `get_hdc`, `get_hdc_device`, `get_abd`, and `get_abd_device` helper contracts. Executable path inputs SHALL retain string-or-Path acceptance and use lexical invocation-relative absolutization without symlink resolution. Device handles SHALL remain usable by `execute_flow` without callers importing `src` modules.

#### Scenario: HDC helper accepts path-like input
- **WHEN** a caller invokes `lib.get_hdc()` and `lib.get_hdc_device()` with an HDC executable path and HDC handle
- **THEN** the system returns a device handle usable by `lib.execute_flow()`

#### Scenario: Device helper wraps HDC handle
- **WHEN** a caller invokes `lib.get_hdc_device()` with an HDC handle
- **THEN** the system returns a device handle usable by `lib.execute_flow()`

#### Scenario: ABD helper accepts path-like input
- **WHEN** a caller invokes the ABD helper with a string or `pathlib.Path` ABD executable path
- **THEN** the system returns an ABD handle usable by the public Andr0id device helper

#### Scenario: Andr0id device helper wraps ABD handle
- **WHEN** a caller invokes the Andr0id device helper with an ABD handle returned by the ABD helper
- **THEN** the system returns a device handle usable by `lib.execute_flow()`

#### Scenario: Public script avoids src imports
- **WHEN** a user follows documented programmatic API examples
- **THEN** the script imports from `lib.py` and does not need to import from `src.*`

### Requirement: Programmatic API creates independent views
The facade SHALL expose `create_view(dirs: list[Path], dst: Path, smaps_filter: Pattern[str] = re.compile(".*")) -> Path`. It SHALL reject an empty source list, duplicate normalized absolute source paths, None/bytes/non-pattern filters, invalid evidence, or incompatible evidence. It SHALL preserve source order, parse declared raw evidence directly, and create exactly the new dst using the uniform view contracts. It SHALL return the normalized absolute dst, never access devices or `.env`, never require earlier views, and never overwrite prior destinations or modify evidence files. New partial views SHALL remain on failure. No source/destination ancestry guard or symlink identity resolution SHALL be imposed; callers SHALL choose separate destinations. Library logging configuration SHALL remain caller-owned.

#### Scenario: Multiple views use the same evidence
- **WHEN** a caller supplies the same evidence with different patterns and different new destinations
- **THEN** independent views are created without changing evidence or prior views

#### Scenario: Default filter is omitted
- **WHEN** `create_view` is called without smaps_filter
- **THEN** a compiled string pattern `.*` includes every normalized mapping tag

#### Scenario: Empty and duplicate source lists
- **WHEN** dirs is empty or contains lexical aliases of the same normalized absolute path
- **THEN** the call rejects the input rather than creating a zero-source view or double-counting a source

#### Scenario: Offline evidence is supplied
- **WHEN** valid evidence is copied without parent flow provenance and no backend configuration is available
- **THEN** `create_view` analyzes it without opening a device or requiring parent `flow.json`
