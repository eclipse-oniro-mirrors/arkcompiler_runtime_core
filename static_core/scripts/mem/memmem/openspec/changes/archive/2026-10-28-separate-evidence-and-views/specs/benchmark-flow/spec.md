# Benchmark orchestration delta

## MODIFIED Requirements

### Requirement: CLI accepts benchmark flow input
The system SHALL provide execute_flow.py for collection, create_view.py for offline analysis, and run.py composing both. All CLIs, including compare.py and record_hdc.py, SHALL explicitly disable argument abbreviation and independently expose/own `--memmem-log-level {info,warn,err}` (default err) and optional `--memmem-log-file`. Execution CLIs SHALL accept required --flow, --backend {hdc,abd} default hdc, optional --out-dir, positive --repeats default 1, mutually exclusive --reboot/--no-reboot default false, and mutually exclusive --device-log/--no-device-log default true. Old --hilog/--no-hilog and --logs/--no-logs SHALL not be supported. They SHALL preprocess JSON through the public canonicalization API, preserve description metadata, construct the selected backend from optional .env HDC_PATH/ABD_PATH values, and require only the selected executable path before device work. run.py SHALL also accept --smaps-filter, compiled during argument parsing with default `.*`; execute_flow.py SHALL not accept report-filter options. Shared CLI setup SHALL not perform work at import time.

#### Scenario: Execution defaults are used
- **WHEN** either execution CLI is invoked with only a valid --flow
- **THEN** it uses HDC, one repeat, no reboot, enabled device logs, error-only memmem logs and a timestamped default memmem-out directory

#### Scenario: Selected backend configuration is missing
- **WHEN** the selected backend's executable path is absent or empty
- **THEN** execution fails before device actions, while a missing unselected backend path is accepted

#### Scenario: Invalid CLI options are supplied
- **WHEN** flow is omitted, repeats is nonpositive, backend/log level is invalid, mutually exclusive toggles conflict, a removed option is supplied, or an option name is abbreviated
- **THEN** argument validation fails

#### Scenario: Invalid regex is supplied to run
- **WHEN** run.py receives an invalid --smaps-filter expression
- **THEN** argparse rejects it before collection or output generation

#### Scenario: Help is requested
- **WHEN** a user requests --help
- **THEN** the selected CLI describes its applicable execution/view/logging options without requiring a device or performing work

#### Scenario: Explicit device log controls are used
- **WHEN** either execution CLI receives --device-log or --no-device-log
- **THEN** it enables or disables device logging respectively, defaults to enabled if neither is supplied, and rejects the pair supplied together

#### Scenario: HDC backend is selected by default
- **WHEN** either execution CLI omits --backend
- **THEN** it selects hdc and requires its configured executable

#### Scenario: User requests help
- **WHEN** --help is requested
- **THEN** applicable execution/view/logging controls are described without device access

#### Scenario: User omits flow argument
- **WHEN** an execution CLI is invoked without --flow
- **THEN** argparse rejects the invocation

#### Scenario: User provides flow input
- **WHEN** an execution CLI receives a valid --flow and no optional controls
- **THEN** it uses the execution defaults specified above

#### Scenario: User provides out dir explicitly
- **WHEN** --out-dir is supplied
- **THEN** it is the parent for canonical flow and iteration evidence, not the exact library destination

#### Scenario: User provides an existing out dir
- **WHEN** an execution CLI receives an existing parent without flow/evidence conflicts
- **THEN** it accepts the parent and exclusively creates the intended new outputs

#### Scenario: User provides repeats explicitly
- **WHEN** --repeats 3 is supplied
- **THEN** the execution CLI schedules iteration_0 through iteration_2

#### Scenario: User provides a non-positive repeats value
- **WHEN** --repeats is zero or negative
- **THEN** argparse rejects the invocation

#### Scenario: User requests reboot explicitly
- **WHEN** --reboot is supplied
- **THEN** reboot is enabled for each execution

#### Scenario: User disables reboot explicitly
- **WHEN** --no-reboot is supplied
- **THEN** reboot is disabled

#### Scenario: User enables hilog explicitly
- **WHEN** the removed --hilog option is supplied
- **THEN** argparse rejects it; the supported replacement is --device-log

#### Scenario: User disables hilog explicitly
- **WHEN** the removed --no-hilog option is supplied
- **THEN** argparse rejects it; the supported replacement is --no-device-log

#### Scenario: User configures memmem log level
- **WHEN** --memmem-log-level info is supplied
- **THEN** that CLI configures informational memmem logging

#### Scenario: User configures memmem log file
- **WHEN** --memmem-log-file is supplied
- **THEN** that CLI configures the given destination under the existing logging contract

#### Scenario: User provides invalid memmem log level
- **WHEN** an unsupported logging level is supplied
- **THEN** argparse rejects it

#### Scenario: User provides removed log controls
- **WHEN** --logs or --no-logs is supplied
- **THEN** argparse rejects it

#### Scenario: User provides contradictory boolean controls
- **WHEN** both members of a reboot or device-log toggle pair are supplied
- **THEN** argparse rejects the contradictory options

#### Scenario: CLI delegates to public API
- **WHEN** CLI setup succeeds
- **THEN** collection delegates to execute_flow and applicable rendering delegates separately to create_view

#### Scenario: ABD backend is selected
- **WHEN** --backend abd is supplied
- **THEN** the CLI constructs ABD from its configured executable

#### Scenario: Unselected backend path is absent
- **WHEN** only the selected backend has a configured path
- **THEN** setup accepts the absent unselected backend path

#### Scenario: Selected backend path is absent
- **WHEN** the selected backend path is missing or empty
- **THEN** setup fails before device actions with the required configuration name

#### Scenario: Invalid backend is provided
- **WHEN** --backend is neither hdc nor abd
- **THEN** argparse rejects it

### Requirement: CLI path arguments resolve against the invocation working directory
CLI and API host paths SHALL use lexical normalization without symlink resolution. Relative flow, output, logging, evidence, comparison and configured executable paths SHALL be anchored to invocation cwd before later runner cwd changes; absolute paths SHALL also be lexically normalized. Artifact-relative metadata paths SHALL remain portable relative references rather than host-absolutized paths. Duplicate view sources SHALL be rejected by normalized absolute spelling, not inode identity.

#### Scenario: Lexical aliases identify the same source
- **WHEN** sources include `/x/evidence` and `/x/tmp/../evidence`
- **THEN** view input validation rejects the duplicate

#### Scenario: Runner changes cwd
- **WHEN** execution changes its working directory
- **THEN** previously resolved input/output/log/executable paths keep their invocation-relative meaning

#### Scenario: Source uses a symlink alias
- **WHEN** a source path contains a symlink
- **THEN** normalization does not resolve it or promise physical-directory deduplication

#### Scenario: Relative path arguments are invariant to the run working directory
- **WHEN** execution later changes cwd
- **THEN** flow, output, log and backend paths retain their lexically normalized invocation-relative meaning

### Requirement: Benchmark execution manages its working directory
Execution SHALL preserve its existing output-working-directory behavior for command execution and relative local transfer paths, including the HDC/WSL workaround. Its working directory during flow commands SHALL be the exact evidence destination, not the CLI run root. The original cwd SHALL be restored on success or failure. Caller-relative paths SHALL have been resolved before this change; APIs SHALL not promise concurrent thread-safe cwd mutation.

#### Scenario: Flow commands execute
- **WHEN** initialized execution begins commands
- **THEN** its working directory is the evidence destination

#### Scenario: Receive is invoked
- **WHEN** a queued artifact is transferred
- **THEN** the device receives a relative local path that resolves to the intended evidence file

#### Scenario: Execution raises
- **WHEN** any execution phase fails
- **THEN** the original working directory is restored before the public call finishes raising

#### Scenario: Run executes from the output directory
- **WHEN** initialized execution begins flow commands
- **THEN** cwd equals the exact evidence destination

#### Scenario: Working directory is restored after the run
- **WHEN** execution succeeds or fails
- **THEN** cwd is restored to the entry value

#### Scenario: File transfers use run-relative local paths
- **WHEN** a pending artifact is received
- **THEN** its local transfer argument is relative to the execution working directory

### Requirement: Benchmark execution accepts injected dependencies
Single-execution code SHALL accept canonical flow, execution options, evidence storage and an already-created Device without constructing a backend or configuring logging internally. Execution options SHALL contain destination, reboot and device_log only; repeats and report filters SHALL belong to orchestration/view generation. The public execute_flow operation SHALL revalidate flow/support, construct its internal execution dependencies and run evidence collection plus structural validation without reports. Device operations, UI translations, preparation health/timeout policy and existing builder/preprocessing behavior SHALL remain unchanged.

#### Scenario: Tests inject a device
- **WHEN** a test supplies canonical flow, evidence store, options and a fake device
- **THEN** lower-level execution uses those dependencies without production construction or report generation

#### Scenario: Public execution is called
- **WHEN** execute_flow receives flow, device, dst and execution flags
- **THEN** it performs one evidence collection and structural validation without repeats, filtering or plots

#### Scenario: CLI constructs production device and options
- **WHEN** execution CLI setup succeeds
- **THEN** it supplies canonical flow, selected production Device and execution options to the public operation with CLI-owned logging

#### Scenario: Public API creates store and reports
- **WHEN** the replacement public execute_flow operation is invoked
- **THEN** it creates internal evidence storage and collects validated evidence but creates no reports; reporting requires a separate create_view call

#### Scenario: Runner uses provided flow, device, store, options, and configured logger
- **WHEN** lower-level execution receives injected dependencies
- **THEN** it uses them for commands, evidence paths, reboot/device_log behavior and existing logger messages without repeats or report filters

#### Scenario: Tests pass fake device, explicit flow, options, and store directly
- **WHEN** runner tests inject those dependencies
- **THEN** no production device or logger construction must be monkeypatched

### Requirement: Repeated runs produce iteration output directories
Execution CLIs SHALL create a missing parent --out-dir or accept a pre-created parent. They SHALL persist one canonical flow.json with description and expanded macros at that root and no run metadata, then use exact new `iteration_i/evidence` API destinations for i from 0 to repeats-1, including repeats=1. Root flow provenance SHALL not be overwritten: an existing flow.json or intended evidence destination SHALL cause an error rather than implicit resume. Direct API execution SHALL not write parent flow.json. Reboot SHALL apply before each requested execution. run.py SHALL collect all iterations before any view, then create individual `iteration_i/view/view-T` views and one combined `view/view-T`, reusing one invocation timestamp T even for a single repeat. execute_flow.py SHALL create no views. Collection failure SHALL stop subsequent iterations and all rendering; rendering failure SHALL stop subsequent views and leave all existing/partial outputs.

#### Scenario: One repeat succeeds
- **WHEN** run.py runs once
- **THEN** it writes iteration_0/evidence and both individual and combined views, with canonical flow only at the root

#### Scenario: Parent exists without conflicting output
- **WHEN** --out-dir is pre-created and flow/evidence targets are available
- **THEN** the CLI accepts the parent and each API exclusively creates its own subdirectory

#### Scenario: Collection fails after an earlier success
- **WHEN** iteration_i fails
- **THEN** later iterations and the entire rendering phase are skipped and all written evidence remains

#### Scenario: Rendering fails
- **WHEN** a per-iteration view fails after collection completed
- **THEN** later individual/combined views are not attempted and evidence and partial views remain

#### Scenario: Repeats equals one
- **WHEN** run.py uses repeats 1
- **THEN** it still creates iteration_0/evidence and both individual and combined views

#### Scenario: Repeats is greater than one
- **WHEN** repeats exceeds one
- **THEN** evidence is collected into iteration_0 through iteration_{repeats-1} before run.py starts any view

#### Scenario: Existing repeated-run output is rejected
- **WHEN** an intended evidence destination or canonical flow file already exists in the chosen parent
- **THEN** collection rejects the conflict without overwriting it; existence of the parent alone is no longer an error

#### Scenario: Iteration failure aborts the run
- **WHEN** an iteration fails
- **THEN** later iterations and all rendering are skipped while partial evidence is retained

#### Scenario: Reboot applies per iteration
- **WHEN** reboot is enabled
- **THEN** it is requested separately for each execution

### Requirement: Backend command support is validated before execution
The public execute_flow entry SHALL check every canonical command immediately after flow revalidation and before evidence-store creation/reservation or device preparation. It SHALL report the first unsupported command. The shared CLI collection helper SHALL preflight support before writing run provenance where practical, while the public API SHALL retain its own validation for direct callers.

#### Scenario: Backend rejects a command
- **WHEN** a canonical command is unsupported by the selected device
- **THEN** execute_flow fails before evidence creation or device actions

#### Scenario: Device rejects an unsupported command
- **WHEN** the selected device reports a canonical command unsupported
- **THEN** execute_flow fails before store creation/reservation, preparation or app launch

### Requirement: Snapshot expectations precede capture and missing PIDs are fatal
A snapshot SHALL record its label, timestamp, and ordered expected files from active apps before liveness checks and capture. The declaration and capture SHALL use the same active-app collection. An unresolved launch PID or missing stored PID for an expected snapshot SHALL raise an error identifying app and PID context and abort execution, not warn and continue or remove the app silently. Successfully captured prefix files SHALL be queued immediately for final receiving even if a later capture fails. Expected declarations SHALL remain in partial metadata. Explicitly terminated labels SHALL remain excluded from subsequent expected snapshots while retained in `apps.json`. PID rediscovery after external relaunch SHALL not be introduced. An empty expected file list for an invoked capture event SHALL be rejected; zero commands of that category instead produce an empty artifact array.

#### Scenario: Stored PID disappears
- **WHEN** a snapshot finds an expected stored PID absent
- **THEN** it raises, later commands are not executed, expected declarations remain, and the CLI reports failure

#### Scenario: App was explicitly terminated
- **WHEN** an earlier `AppFlow` requested termination
- **THEN** later snapshots do not declare or inspect that removed label

#### Scenario: Later capture fails
- **WHEN** one app capture succeeds and a subsequent one fails
- **THEN** the successful prefix remains queued for receiving and execution still fails

### Requirement: Benchmark execution captures run-wide hilog
Execution SHALL collect one backend device-log stream per execution when device_log is true, using HDC hilog or ABD logcat with the existing lifecycle and fixed evidence path `logs/log.log`. It SHALL not clear/dump logs per AppFlow or delay requested termination for a per-app dump. Stream shutdown and pending receives SHALL still be attempted after execution failures. Disabled device logging SHALL skip configuration, start, stop, tracking and receive. Memmem application logs SHALL remain separate.

#### Scenario: Enabled execution succeeds
- **WHEN** all AppFlows succeed with device logs enabled
- **THEN** one log artifact is received at logs/log.log after stream shutdown

#### Scenario: Flow and shutdown both fail
- **WHEN** flow execution fails and stream shutdown also fails
- **THEN** the primary flow error is preserved with shutdown context and pending receives are still attempted

#### Scenario: Device logging is disabled
- **WHEN** device_log is false
- **THEN** no log lifecycle operations occur and no log evidence is required

#### Scenario: Hihilog collection is enabled for successful benchmark
- **WHEN** execution succeeds with device_log true
- **THEN** it receives one backend log stream at logs/log.log after shutdown

#### Scenario: Hihilog collection is enabled for failing benchmark
- **WHEN** execution fails after enabled stream startup
- **THEN** shutdown and pending receives are still attempted

#### Scenario: AppFlow and log stream stop both fail
- **WHEN** an AppFlow error is followed by a stream stop error
- **THEN** the AppFlow error is preserved with additional stop-failure context

#### Scenario: Hihilog collection is disabled
- **WHEN** device_log is false
- **THEN** log configuration, streaming, tracking and receiving are skipped

#### Scenario: Termination does not wait for per-AppFlow dump
- **WHEN** an AppFlow requests termination with device logging enabled
- **THEN** termination proceeds after its commands without a per-AppFlow dump

### Requirement: Commands execute with action-specific semantics
The system SHALL support command actions `wait`, `snapshot`, `screenshot`, `key`, `tap`, `double_tap`, `long_tap`, `swipe`, `drag`, `fling`, `directional_fling`, `input_text`, `text`, and `multi_touch_input`, and schema validation MUST enforce each action's payload type before execution. `multi_touch_input` SHALL use a payload containing `finger_list`, a tuple of exactly two or three strict `Displacement` values; each `Displacement` SHALL contain strict integer `x1_pct`, `y1_pct`, `x2_pct`, and `y2_pct` values in `0..100`. Existing `SwipePayload` SHALL inherit `Displacement`. `multi_touch_input.duration_ms` SHALL be a strict integer in `1..15000`.

#### Scenario: Wait command executes
- **WHEN** a `wait` command has a non-negative numeric payload (integer or float)
- **THEN** the system waits for the requested number of seconds before proceeding

#### Scenario: Wait command rejects invalid payloads
- **WHEN** a `wait` command payload is negative or non-numeric
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Snapshot command executes
- **WHEN** a `snapshot` command has a valid globally unique string payload
- **THEN** the system records one timestamp and ordered expected files from the eligible active_apps list before liveness checks, excludes explicitly terminated labels, fails if any expected PID is missing, and immediately queues every successful capture for later receiving

#### Scenario: Labels share a PID but one is no longer eligible
- **WHEN** launched labels share a stored PID and one label has been explicitly removed by its requested termination
- **THEN** a later snapshot excludes that label even if the stored PID still exists, and any remaining eligible label is subject to the fatal missing-PID rule

#### Scenario: Screenshot command executes
- **WHEN** a `screenshot` command has a valid globally unique screenshot label payload
- **THEN** the system records one screenshot timestamp and expected PNG declaration before capture, captures the current screen to the declared device-local file, and queues the successful artifact for transfer

#### Scenario: Key command executes
- **WHEN** a `key` command has a valid named key payload
- **THEN** the system delegates the key press to the device layer

#### Scenario: Tap command executes
- **WHEN** a `tap` command has `x_pct` and `y_pct` integer payload values in `0..100`
- **THEN** the system converts the normalized coordinate to a cached screen pixel point and delegates a tap to the device layer

#### Scenario: Double tap command executes
- **WHEN** a `double_tap` command has `x_pct` and `y_pct` integer payload values in `0..100`
- **THEN** the system converts the normalized coordinate to a cached screen pixel point and delegates a double tap to the device layer

#### Scenario: Long tap command executes
- **WHEN** a `long_tap` command has `x_pct` and `y_pct` integer payload values in `0..100`
- **THEN** the system converts the normalized coordinate to a cached screen pixel point and delegates a long tap to the device layer

#### Scenario: Swipe command executes
- **WHEN** a `swipe` command has normalized start and end coordinates and a velocity in `200..40000`
- **THEN** the system converts both normalized coordinates to cached screen pixel points and delegates a swipe to the device layer

#### Scenario: Drag command executes
- **WHEN** a `drag` command has normalized start and end coordinates and a velocity in `200..40000`
- **THEN** the system converts both normalized coordinates to cached screen pixel points and delegates a drag to the device layer

#### Scenario: Fling command executes
- **WHEN** a `fling` command has normalized start and end coordinates, a velocity in `200..40000`, and a positive step length
- **THEN** the system converts both normalized coordinates to cached screen pixel points and delegates a fling to the device layer

#### Scenario: Directional fling command executes
- **WHEN** a `directional_fling` command has a direction, a velocity in `200..40000`, and a positive step length
- **THEN** the system delegates a directional fling to the device layer

#### Scenario: Input text command executes
- **WHEN** an `input_text` command has a normalized coordinate and non-empty text
- **THEN** the system converts the normalized coordinate to a cached screen pixel point and delegates coordinate-based text input to the device layer

#### Scenario: Text command executes
- **WHEN** a `text` command has non-empty text
- **THEN** the system delegates focused text input to the device layer

#### Scenario: Valid multi-touch command executes
- **WHEN** a `multi_touch_input` command contains a `finger_list` of two through three valid contact paths and a duration in range
- **THEN** the system converts every normalized endpoint to screen coordinates and delegates one synchronized multi-contact gesture to the device layer

#### Scenario: Multi-touch payload has an invalid contact count
- **WHEN** a `multi_touch_input` command payload contains fewer than two contacts or more than three contacts
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Multi-touch coordinate or duration is invalid
- **WHEN** a `multi_touch_input` command contains a coordinate outside `0..100` or a duration outside `1..15000`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Multi-touch displacement has an extra field
- **WHEN** a `multi_touch_input` contact contains a field other than `x1_pct`, `y1_pct`, `x2_pct`, or `y2_pct`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Multi-touch input is not approximated
- **WHEN** a validated flow contains a `multi_touch_input` command
- **THEN** the system does not replace it with one or more sequential single-pointer swipe or drag gestures

#### Scenario: Unknown command is present
- **WHEN** a command action is not supported
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Velocity is outside UITest range
- **WHEN** a swipe-like command payload contains velocity less than `200` or greater than `40000`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Step length is missing for fling
- **WHEN** a `fling` or `directional_fling` command omits `step_length`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Step length is not positive
- **WHEN** a `fling` or `directional_fling` command contains `step_length` less than or equal to zero
- **THEN** the system fails schema validation before performing device actions

### Requirement: Benchmark execution prepares device environment before AppFlows
The benchmark runner SHALL prepare the device environment before launching any AppFlow. When reboot is enabled, it SHALL reboot the device, wait for selected-backend availability and boot completion, wake the device, perform an upward directional fling, and send Back. Regardless of reboot setting, it SHALL disable screen timeout before AppFlows. When device_log is enabled, it SHALL configure the selected backend's device logging before health verification and remote setup. It SHALL verify device health before remote output setup, then create remote output and prepare execution context before starting any enabled run-wide log stream. After startup validation and enabled stream startup, it SHALL launch AppFlows. This change SHALL preserve existing preparation ordering, timeouts and health policy; backend-neutral terminology SHALL not imply a new device operation.

#### Scenario: Reboot is enabled
- **WHEN** benchmark options have reboot equal to True
- **THEN** the runner reboots, waits for selected-backend availability and boot completion, performs the post-reboot wake routine, configures enabled device logging, verifies health, creates remote output, reads screen bounds, and starts enabled streaming before launching AppFlows

#### Scenario: Reboot is disabled
- **WHEN** benchmark options have reboot equal to False
- **THEN** the runner skips reboot, availability/boot waits and post-reboot wake routine while still configuring enabled logging, verifying health, creating remote output, reading screen bounds and starting enabled streaming before AppFlows

#### Scenario: Post-reboot wake routine runs
- **WHEN** reboot preparation reaches boot completion
- **THEN** the runner wakes the device, performs an upward directional fling and sends Back before disabling screen timeout

#### Scenario: Device health is acceptable
- **WHEN** benchmark execution prepares the device environment and device health reports battery capacity at least 30% and every readable nonzero thermal zone at or below 80000 millidegrees Celsius
- **THEN** the runner continues preparation and may launch AppFlows

#### Scenario: Battery capacity is too low
- **WHEN** benchmark execution prepares the device environment and device health reports battery capacity below 30%
- **THEN** the runner rejects startup before starting any enabled log stream, reading screen bounds or launching AppFlows

#### Scenario: Thermal zone is too hot
- **WHEN** benchmark execution prepares the device environment and any readable nonzero thermal zone reports a temperature above 80000 millidegrees Celsius
- **THEN** the runner rejects startup before starting any enabled log stream, reading screen bounds or launching AppFlows

#### Scenario: Screen timeout is disabled
- **WHEN** benchmark execution prepares the device environment
- **THEN** the runner disables screen timeout before health verification and AppFlows independent of reboot and log options

#### Scenario: Device logging is enabled during preparation
- **WHEN** benchmark options have device_log equal to True
- **THEN** the runner configures the selected backend's logging before remote output setup and starts streaming after remote setup, health verification and execution context preparation

#### Scenario: Hihilog collection is enabled during preparation
- **WHEN** the replacement device_log option is True
- **THEN** selected-backend log configuration and streaming follow the preserved preparation order, without requiring a removed hilog option

## REMOVED Requirements

### Requirement: Snapshot skips invalid stored PIDs
**Reason**: Silently reducing eligibility violates the new successful-execution structural-validity contract.
**Migration**: Missing expected PIDs now fail execution; explicit AppFlow termination remains the only lifecycle-based removal covered here.

## MODIFIED Requirements

### Requirement: Offline view CLI creates a new independently named view
The system SHALL provide `create_view.py DIR1 [DIR2 ...] --out-dir PARENT [--smaps-filter REGEX]` with one or more positional exact evidence roots in argument order. It SHALL use compiled `.*` by default, reject invalid regex at parse time, accept an existing output parent, and create only a new `PARENT/view/view-YYYYMMDD_HHMMSS_microseconds` through create_view. Each invocation SHALL choose a fresh host timestamp; collision SHALL fail without overwrite. It SHALL require no .env, device executable, flow.json, prior view, iteration discovery or backend options. summarize.py SHALL be removed, not retained as a legacy redirect.

#### Scenario: User generates another view
- **WHEN** an existing parent contains earlier views and valid evidence paths are explicitly supplied
- **THEN** a new timestamped view is created without changing earlier views or evidence

#### Scenario: Exact generated destination collides
- **WHEN** the generated view destination already exists
- **THEN** creation fails rather than merging, deleting or replacing it
