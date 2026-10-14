# Programmatic interface delta

## MODIFIED Requirements

### Requirement: Programmatic API runs validated flows
The public facade SHALL expose `execute_flow(flow: Flow, device: Device, dst: Path, *, reboot: bool = False, device_log: bool = True) -> Path` in place of `run`. It SHALL revalidate canonical flow and all backend command support before evidence reservation/device actions, execute exactly once, receive evidence, perform shared local structural validation, and return the lexically normalized absolute destination. It SHALL exclusively create the exact dst, never append a layout prefix, and reject existing destinations without modifying them. It SHALL NOT repeat execution, accept a smaps filter, generate reports, write parent flow provenance, configure/reset logging, or convert failures to statuses. Newly created partial evidence SHALL remain on failure. Removed `run`, `logs`, and `hilog` interfaces SHALL NOT be retained as compatibility shims. Public configure_logger/get_logger/reset_logger helpers SHALL remain separately available.

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
- **WHEN** execute_flow receives device_log false
- **THEN** device log setup, stream handling and receive are disabled without affecting memmem application logging

#### Scenario: Programmatic run succeeds
- **WHEN** the replacement execute_flow API completes
- **THEN** it returns normalized evidence dst without generating reports

#### Scenario: Runtime options are keyword-only
- **WHEN** execute_flow is called
- **THEN** reboot and device_log are keyword-only, while flow, device and dst follow the declared new signature

#### Scenario: Runtime option defaults match CLI
- **WHEN** execute_flow omits optional controls
- **THEN** reboot is False and device_log is True; repeats and filters are not execution options

#### Scenario: Programmatic destination resolves against the caller's working directory
- **WHEN** dst is relative
- **THEN** execute_flow anchors and lexically normalizes it at entry and returns its absolute form

#### Scenario: Repeated execution is caller-orchestrated
- **WHEN** a caller needs repeated execution
- **THEN** it invokes execute_flow once per new evidence destination or uses CLI repeats; execute_flow does not accept repeats

#### Scenario: Legacy execution keywords are removed
- **WHEN** logs, hilog, repeats, or smaps_filter is supplied to execute_flow
- **THEN** the unexpected keyword is rejected

#### Scenario: Programmatic out dir resolves against the caller's working directory
- **WHEN** dst is relative
- **THEN** execute_flow anchors and lexically normalizes it at entry and returns its absolute form

#### Scenario: Programmatic out dir already exists
- **WHEN** exact dst exists
- **THEN** execute_flow raises without modifying it or performing device actions

#### Scenario: Repeated run root already exists
- **WHEN** a programmatic caller requests an execution at an existing exact destination
- **THEN** it is rejected regardless of iteration children; parent reuse and repeats belong to CLI orchestration instead

#### Scenario: Programmatic flow is revalidated
- **WHEN** a canonical Flow was mutated into an invalid state
- **THEN** execute_flow rejects it before device actions

#### Scenario: Programmatic run failure raises
- **WHEN** execute_flow or create_view fails
- **THEN** the exception reaches the caller rather than a status return

#### Scenario: Programmatic repeats greater than one
- **WHEN** a caller needs repeated execution
- **THEN** it invokes execute_flow once per new evidence destination or uses CLI repeats; execute_flow does not accept repeats

#### Scenario: Programmatic repeats is non-positive
- **WHEN** a repeats keyword of any value is passed to execute_flow
- **THEN** the unexpected keyword is rejected; positive-repeat validation belongs to CLI orchestration

#### Scenario: Programmatic device-log collection is disabled
- **WHEN** execute_flow receives device_log=False
- **THEN** all device-log lifecycle operations are disabled

#### Scenario: Programmatic smaps filter restricts reports
- **WHEN** a caller passes a compiled pattern to create_view
- **THEN** only matching mappings contribute, while execute_flow accepts no report filter

#### Scenario: Programmatic smaps filter default does not restrict reports
- **WHEN** create_view omits the pattern
- **THEN** compiled `.*` includes all tags

#### Scenario: Programmatic legacy log keywords are removed
- **WHEN** logs or hilog is supplied to execute_flow
- **THEN** the unexpected keyword is rejected

#### Scenario: Programmatic memmem log level is configured
- **WHEN** a caller configures info logging before either new API
- **THEN** the API honors and preserves that logger configuration

#### Scenario: Programmatic memmem log file is configured
- **WHEN** a caller configures a log file before either new API
- **THEN** emitted memmem logs use that destination and the API does not reset it

### Requirement: Programmatic API constructs flows with thin wrappers
The facade SHALL preserve `flow`, `app_flow`, all existing canonical command builders, and unprocessed_flow/unprocessed_app_flow/unprocessed_snapshot/unprocessed_screenshot/repeat builders as thin validated-model wrappers with no hidden defaults beyond their underlying models. Flow description SHALL remain optional. App-flow construction SHALL require label, bundle, ability, terminate and commands. Repeat construction SHALL require n_iter, iter_var and commands and accept empty commands under existing validation. Builders SHALL raise validation errors immediately. `app` SHALL remain absent from public exports. Canonical builder output SHALL be executable through execute_flow; macro-bearing output SHALL first pass through the unchanged public preprocess_flow.

#### Scenario: Python constructs a canonical scenario
- **WHEN** a caller combines builders and Python loops
- **THEN** the resulting Flow is accepted by execute_flow and preserves optional description metadata

#### Scenario: Builder payload is invalid
- **WHEN** a caller supplies invalid labels, repeat parameters or command payloads
- **THEN** the wrapper fails during model construction

#### Scenario: Text and unprocessed builders are used
- **WHEN** text receives a string or unprocessed snapshot builders receive macro placeholders
- **THEN** text retains its flat string payload and unprocessed placeholders remain subject to existing preprocess/canonical validation rather than implicit execution preprocessing

#### Scenario: Flow wrapper accepts app models
- **WHEN** lib.flow receives app-flow models
- **THEN** it returns a validated Flow

#### Scenario: Flow wrapper accepts description metadata
- **WHEN** lib.flow receives desc
- **THEN** the validated Flow preserves it

#### Scenario: App-flow wrapper requires explicit fields
- **WHEN** lib.app_flow is called
- **THEN** label, bundle, ability, terminate and commands remain required

#### Scenario: Command wrappers validate immediately
- **WHEN** a command builder receives an invalid label or payload
- **THEN** it raises during construction

#### Scenario: Text wrapper constructs a flat payload command
- **WHEN** lib.text receives a string
- **THEN** TextCommand retains the string payload directly

#### Scenario: Unprocessed flow wrapper accepts unprocessed app models
- **WHEN** lib.unprocessed_flow receives unprocessed app models
- **THEN** it returns validated UnprocessedFlow

#### Scenario: Unprocessed app-flow wrapper requires explicit fields
- **WHEN** lib.unprocessed_app_flow is called
- **THEN** label, bundle, ability, terminate and commands remain explicit

#### Scenario: Unprocessed snapshot and screenshot wrappers construct lenient commands
- **WHEN** unprocessed artifact builders receive placeholder-bearing labels
- **THEN** labels remain subject to existing preprocess/canonical validation

#### Scenario: Repeat wrapper requires explicit payload fields
- **WHEN** lib.repeat is called
- **THEN** n_iter, iter_var and commands remain required and validated, including accepted empty commands

#### Scenario: Unprocessed builder validation failures are surfaced
- **WHEN** unprocessed builders receive invalid fields
- **THEN** construction fails immediately

#### Scenario: Repetitive scenarios can be generated with Python
- **WHEN** callers combine builders through Python loops/helpers
- **THEN** resulting canonical flows can be passed to execute_flow

#### Scenario: Old app wrapper is not public
- **WHEN** facade exports are inspected
- **THEN** app_flow is public and app remains absent

### Requirement: Programmatic API exports public type names
The facade SHALL continue exporting existing public flow, app-flow, command, Device, Hdc, Abd, unprocessed flow/app-flow, Macro, RepeatMacro, UnprocessedSnapshotCommand and UnprocessedScreenshotCommand types and preprocess_flow for typed scripts without src imports. It SHALL export execute_flow and create_view and SHALL NOT export run. Payload structs/aliases, UnprocessedCommand, generic model bases, storage/parsed objects, equivalence helpers, and internal runner options SHALL remain internal.

#### Scenario: Typed user script uses the facade
- **WHEN** a script annotates builders, Flow, Command or device handles and calls the two new operations
- **THEN** it type-checks using lib and standard-library types without importing internal models

#### Scenario: Internal types are inspected
- **WHEN** public exports are inspected
- **THEN** payload types, storage/parsed models and internal orchestration types are not added to the public facade

#### Scenario: Typed user script annotates flow helpers
- **WHEN** scripts use lib.Flow, lib.AppFlow or lib.Command
- **THEN** mypy resolves those types without internal imports

#### Scenario: Typed user script annotates device handles
- **WHEN** scripts annotate public device/backend handle types
- **THEN** those types remain available through lib

#### Scenario: Existing HDC type remains available
- **WHEN** scripts import lib.Hdc and lib.Device
- **THEN** both remain supported

#### Scenario: Precise command types are available
- **WHEN** scripts annotate precise public command types
- **THEN** those existing exports remain available

#### Scenario: Typed user script annotates unprocessed types
- **WHEN** scripts use public unprocessed and macro types
- **THEN** mypy resolves the existing supported type names through lib

#### Scenario: Preprocessing function is available from the facade
- **WHEN** scripts call or annotate lib.preprocess_flow
- **THEN** no src import is required

#### Scenario: Payload structs remain private
- **WHEN** facade exports are inspected
- **THEN** payload structs and aliases remain internal

#### Scenario: Unprocessed command union remains private
- **WHEN** facade exports are inspected
- **THEN** UnprocessedCommand remains internal

#### Scenario: Internal runner options remain private
- **WHEN** facade exports are inspected
- **THEN** internal execution options and storage/parsed types remain internal

### Requirement: Programmatic API creates device handles through public helpers
The system SHALL preserve get_hdc/get_hdc_device and get_abd/get_abd_device helper contracts. Executable path inputs SHALL retain string-or-Path acceptance and use lexical invocation-relative absolutization without symlink resolution. Device handles SHALL remain usable by execute_flow without callers importing src modules.

#### Scenario: Existing device construction is reused
- **WHEN** a caller constructs either backend through public helpers
- **THEN** the resulting Device can be passed to execute_flow with unchanged backend behavior

#### Scenario: Relative executable path is provided
- **WHEN** an executable path contains relative components
- **THEN** the helper anchors and lexically normalizes it before any runner cwd change without resolving symlinks

#### Scenario: HDC helper accepts path-like input
- **WHEN** get_hdc receives a string or Path executable location
- **THEN** it returns an Hdc handle using the normalized absolute executable path

#### Scenario: Device helper wraps HDC handle
- **WHEN** get_hdc_device receives an Hdc handle
- **THEN** it returns a Device usable by execute_flow

#### Scenario: ABD helper accepts path-like input
- **WHEN** get_abd receives a string or Path executable location
- **THEN** it returns an Abd handle using the normalized absolute executable path

#### Scenario: Andr0id device helper wraps ABD handle
- **WHEN** get_abd_device receives an Abd handle
- **THEN** it returns a Device usable by execute_flow

#### Scenario: Public script avoids src imports
- **WHEN** a script follows the documented programmatic API
- **THEN** it uses lib and standard-library types without src imports

## MODIFIED Requirements

### Requirement: Programmatic API creates independent views
The facade SHALL expose `create_view(dirs: list[Path], dst: Path, smaps_filter: Pattern[str] = re.compile(".*")) -> Path`. It SHALL reject an empty source list, duplicate normalized absolute source paths, None/bytes/non-pattern filters, invalid evidence, or incompatible evidence. It SHALL preserve source order, parse declared raw evidence directly, and create exactly the new dst using the uniform view contracts. It SHALL return the normalized absolute dst, never access devices or .env, never require earlier views, and never overwrite prior destinations or modify evidence files. New partial views SHALL remain on failure. No source/destination ancestry guard or symlink identity resolution SHALL be imposed; callers SHALL choose separate destinations. Library logging configuration SHALL remain caller-owned.

#### Scenario: Multiple views use the same evidence
- **WHEN** a caller supplies the same evidence with different patterns and different new destinations
- **THEN** independent views are created without changing evidence or prior views

#### Scenario: Default filter is omitted
- **WHEN** create_view is called without smaps_filter
- **THEN** a compiled string pattern `.*` includes every normalized mapping tag

#### Scenario: Empty and duplicate source lists
- **WHEN** dirs is empty or contains lexical aliases of the same normalized absolute path
- **THEN** the call rejects the input rather than creating a zero-source view or double-counting a source

#### Scenario: Offline evidence is supplied
- **WHEN** valid evidence is copied without parent flow provenance and no backend configuration is available
- **THEN** create_view analyzes it without opening a device or requiring parent flow.json
