# benchmark-flow Specification

## Purpose
Defines the benchmark CLI, flow schema, app launch behavior, command semantics, and PID handling for benchmark execution.

## Requirements

### Requirement: CLI accepts benchmark flow input
The system SHALL provide `execute_flow.py` for collection, `create_view.py` for offline analysis, and `run.py` composing both. All CLIs, including `compare.py` and `record_hdc.py`, SHALL explicitly disable argument abbreviation and independently expose and own `--memmem-log-level {info,warn,err}` (default `err`) and optional `--memmem-log-file`. Execution CLIs SHALL accept required `--flow`, `--backend {hdc,abd}` defaulting to `hdc`, optional `--out-dir`, positive `--repeats` defaulting to `1`, mutually exclusive `--reboot` and `--no-reboot` defaulting to false, and mutually exclusive `--device-log` and `--no-device-log` defaulting to true. Old `--hilog`, `--no-hilog`, `--logs`, and `--no-logs` SHALL not be supported. They SHALL preprocess JSON through the public canonicalization API, preserve description metadata, construct the selected backend from optional `.env` `HDC_PATH` and `ABD_PATH` values, and require only the selected executable path before device work. `run.py` SHALL also accept `--smaps-filter`, compiled during argument parsing with default `.*`; `execute_flow.py` SHALL not accept report-filter options. Shared CLI setup SHALL not perform work at import time.

#### Scenario: Execution defaults are used
- **WHEN** either execution CLI is invoked with only a valid `--flow`
- **THEN** it uses HDC, one repeat, no reboot, enabled device logs, error-only memmem logs, and a timestamped default `memmem-out` directory

#### Scenario: Selected backend configuration is missing
- **WHEN** the selected backend's executable path is absent or empty
- **THEN** execution fails before device actions, while a missing unselected backend path is accepted

#### Scenario: Invalid CLI options are supplied
- **WHEN** flow is omitted, repeats is nonpositive, backend or log level is invalid, mutually exclusive toggles conflict, a removed option is supplied, or an option name is abbreviated
- **THEN** argument validation fails

#### Scenario: Invalid regex is supplied to run
- **WHEN** `run.py` receives an invalid `--smaps-filter` expression
- **THEN** argparse rejects it before collection or output generation

#### Scenario: Help is requested
- **WHEN** a user requests `--help`
- **THEN** the selected CLI describes its applicable execution, view, and logging options without requiring a device or performing work

#### Scenario: Explicit device log controls are used
- **WHEN** either execution CLI receives `--device-log` or `--no-device-log`
- **THEN** it enables or disables device logging respectively, defaults to enabled if neither is supplied, and rejects the pair supplied together

#### Scenario: HDC backend is selected by default
- **WHEN** either execution CLI omits `--backend`
- **THEN** it selects HDC and requires its configured executable

#### Scenario: User provides flow input
- **WHEN** an execution CLI receives a valid `--flow` and no optional controls
- **THEN** it uses the execution defaults specified above

#### Scenario: User provides out dir explicitly
- **WHEN** `--out-dir` is supplied
- **THEN** it is the parent for canonical flow and iteration evidence, not the exact library destination

#### Scenario: User provides an existing out dir
- **WHEN** an execution CLI receives an existing parent without flow or evidence conflicts
- **THEN** it accepts the parent and exclusively creates the intended new outputs

#### Scenario: User provides repeats explicitly
- **WHEN** `--repeats 3` is supplied
- **THEN** the execution CLI schedules `iteration_0` through `iteration_2`

#### Scenario: User requests reboot explicitly
- **WHEN** `--reboot` is supplied
- **THEN** reboot is enabled for each execution

#### Scenario: User disables reboot explicitly
- **WHEN** `--no-reboot` is supplied
- **THEN** reboot is disabled

#### Scenario: User enables hilog explicitly
- **WHEN** the removed `--hilog` option is supplied
- **THEN** argparse rejects it; the supported replacement is `--device-log`

#### Scenario: User disables hilog explicitly
- **WHEN** the removed `--no-hilog` option is supplied
- **THEN** argparse rejects it; the supported replacement is `--no-device-log`

#### Scenario: User configures memmem log level
- **WHEN** `--memmem-log-level info` is supplied
- **THEN** that CLI configures informational memmem logging

#### Scenario: User configures memmem log file
- **WHEN** `--memmem-log-file` is supplied
- **THEN** that CLI configures the given destination under the existing logging contract

#### Scenario: User provides removed log controls
- **WHEN** `--logs` or `--no-logs` is supplied
- **THEN** argparse rejects it

#### Scenario: User provides contradictory boolean controls
- **WHEN** both members of a reboot or device-log toggle pair are supplied
- **THEN** argparse rejects the contradictory options

#### Scenario: CLI delegates to public API
- **WHEN** CLI setup succeeds
- **THEN** collection delegates to `execute_flow` and applicable rendering delegates separately to `create_view`

#### Scenario: ABD backend is selected
- **WHEN** `--backend abd` is supplied
- **THEN** the CLI constructs ABD from its configured executable

#### Scenario: Unselected backend path is absent
- **WHEN** only the selected backend has a configured path
- **THEN** setup accepts the absent unselected backend path

#### Scenario: Selected backend path is absent
- **WHEN** the selected backend path is missing or empty
- **THEN** setup fails before device actions with the required configuration name

#### Scenario: Invalid backend is provided
- **WHEN** `--backend` is neither `hdc` nor `abd`
- **THEN** argparse rejects it

### Requirement: CLI path arguments resolve against the invocation working directory
CLI and API host paths SHALL use lexical normalization without symlink resolution. Relative flow, output, logging, evidence, comparison, and configured executable paths SHALL be anchored to invocation cwd before later runner cwd changes; absolute paths SHALL also be lexically normalized. Artifact-relative metadata paths SHALL remain portable relative references rather than host-absolutized paths. Duplicate view sources SHALL be rejected by normalized absolute spelling, not inode identity.

#### Scenario: Relative path arguments are invariant to the run working directory
- **WHEN** execution later changes cwd
- **THEN** flow, output, log, and backend paths retain their lexically normalized invocation-relative meaning

#### Scenario: Lexical aliases identify the same source
- **WHEN** sources include `/x/evidence` and `/x/tmp/../evidence`
- **THEN** view input validation rejects the duplicate

#### Scenario: Source uses a symlink alias
- **WHEN** a source path contains a symlink
- **THEN** normalization does not resolve it or promise physical-directory deduplication

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

### Requirement: Benchmark execution accepts injected dependencies
Single-execution code SHALL accept canonical flow, execution options, evidence storage, and an already-created `Device` without constructing a backend or configuring logging internally. Execution options SHALL contain destination, reboot, and `device_log` only; repeats and report filters SHALL belong to orchestration and view generation. The public `execute_flow` operation SHALL revalidate flow and support, construct its internal execution dependencies, and run evidence collection plus structural validation without reports. Device operations, UI translations, preparation health and timeout policy, and existing builder and preprocessing behavior SHALL remain unchanged.

#### Scenario: Tests inject a device
- **WHEN** a test supplies canonical flow, evidence store, options, and a fake device
- **THEN** lower-level execution uses those dependencies without production construction or report generation

#### Scenario: Public execution is called
- **WHEN** `execute_flow` receives flow, device, destination, and execution flags
- **THEN** it performs one evidence collection and structural validation without repeats, filtering, or plots

#### Scenario: CLI constructs production device and options
- **WHEN** execution CLI setup succeeds
- **THEN** it supplies canonical flow, selected production `Device`, and execution options to the public operation with CLI-owned logging

#### Scenario: Public API creates store and reports
- **WHEN** the replacement public `execute_flow` operation is invoked
- **THEN** it creates internal evidence storage and collects validated evidence but creates no reports; reporting requires a separate `create_view` call

#### Scenario: Runner uses provided flow, device, store, options, and configured logger
- **WHEN** lower-level execution receives injected dependencies
- **THEN** it uses them for commands, evidence paths, reboot and `device_log` behavior, and existing logger messages without repeats or report filters

### Requirement: Repeated runs produce iteration output directories
Execution CLIs SHALL create a missing parent `--out-dir` or accept a pre-created parent. They SHALL persist one canonical `flow.json` with description and expanded macros at that root and no run metadata, then use exact new `iteration_i/evidence` API destinations for `i` from `0` to `repeats - 1`, including `repeats=1`. Root flow provenance SHALL not be overwritten: an existing `flow.json` or intended evidence destination SHALL cause an error rather than implicit resume. Direct API execution SHALL not write parent `flow.json`. Reboot SHALL apply before each requested execution. `run.py` SHALL collect all iterations before any view, then create individual `iteration_i/view/view-T` views and one combined `view/view-T`, reusing one invocation timestamp `T` even for a single repeat. `execute_flow.py` SHALL create no views. Collection failure SHALL stop subsequent iterations and all rendering; rendering failure SHALL stop subsequent views and leave all existing or partial outputs.

#### Scenario: Repeats equals one
- **WHEN** `run.py` uses repeats `1`
- **THEN** it still creates `iteration_0/evidence` and both individual and combined views

#### Scenario: Repeats is greater than one
- **WHEN** repeats exceeds `1`
- **THEN** evidence is collected into `iteration_0` through `iteration_{repeats-1}` before `run.py` starts any view

#### Scenario: Existing repeated-run output is rejected
- **WHEN** an intended evidence destination or canonical flow file already exists in the chosen parent
- **THEN** collection rejects the conflict without overwriting it; existence of the parent alone is no longer an error

#### Scenario: Iteration failure aborts the run
- **WHEN** an iteration fails
- **THEN** later iterations and all rendering are skipped while partial evidence is retained

#### Scenario: Reboot applies per iteration
- **WHEN** reboot is enabled
- **THEN** it is requested separately for each execution

#### Scenario: Parent exists without conflicting output
- **WHEN** `--out-dir` is pre-created and flow and evidence targets are available
- **THEN** the CLI accepts the parent and each API exclusively creates its own subdirectory

#### Scenario: Rendering fails
- **WHEN** a per-iteration view fails after collection completed
- **THEN** later individual and combined views are not attempted and evidence and partial views remain

### Requirement: Flow JSON defines ordered app flows
The system SHALL load and validate a JSON flow file as an unprocessed flow containing an ordered list of `AppFlow` entries with `label`, `bundle`, `ability`, `terminate`, and `commands` fields, and SHALL preprocess the unprocessed flow into a canonical `Flow` through the public preprocessing API before execution. Unprocessed command entries MAY be concrete canonical commands or repeat macro entries. Repeat macro entries MUST be expanded before canonical `Flow` validation and before execution. Canonical `Flow` validation MUST enforce that app labels and snapshot labels match `^[A-Za-z0-9_-]+$`, that screenshot labels match `^[A-Za-z0-9_-]+$`, that app labels are globally unique across the flow, that snapshot labels are globally unique across all expanded `snapshot` commands, that screenshot labels are globally unique across all expanded `screenshot` commands, and that UI coordinate command payloads use integer percent coordinates from `0` to `100`. The flow JSON MAY include optional top-level `"$desc"` string metadata that describes the flow for human reviewers and does not affect execution.

#### Scenario: Valid flow loads successfully
- **WHEN** a flow JSON file contains valid `AppFlow` entries with explicit terminate values, unique app labels, unique snapshot labels, and unique screenshot labels
- **THEN** the system preprocesses the flow and preserves the declared order

#### Scenario: Generated recorder flow loads successfully
- **WHEN** a flow JSON file generated by the UI input recorder contains generated app labels, recorded bundle/ability metadata, and converted UI commands
- **THEN** the system loads the flow as a normal canonical benchmark flow

#### Scenario: Flow description loads successfully
- **WHEN** a flow JSON file contains top-level `"$desc"` with a string value
- **THEN** the system loads the flow and preserves the description metadata without changing benchmark execution

#### Scenario: Flow description must be a string
- **WHEN** a flow JSON file contains top-level `"$desc"` with a non-string value
- **THEN** the system fails validation before performing device actions

#### Scenario: Required field is missing
- **WHEN** a flow JSON file omits a required `AppFlow` field such as `terminate`
- **THEN** the system fails validation before performing device actions

#### Scenario: Invalid JSON is provided
- **WHEN** the flow path points to malformed JSON
- **THEN** the system fails validation before performing device actions

#### Scenario: App label contains unsafe characters
- **WHEN** an unprocessed `AppFlow` label contains characters outside letters, digits, underscore, and hyphen
- **THEN** the system fails canonical validation during preprocessing before performing device actions

#### Scenario: Snapshot label contains unsafe characters
- **WHEN** an expanded `snapshot` command payload contains characters outside letters, digits, underscore, and hyphen
- **THEN** the system fails canonical validation before performing device actions

#### Scenario: Screenshot label contains unsafe characters
- **WHEN** an expanded `screenshot` command payload contains characters outside letters, digits, underscore, and hyphen
- **THEN** the system fails canonical validation before performing device actions

#### Scenario: Duplicate app label is present
- **WHEN** multiple `AppFlow` entries use the same label
- **THEN** the system fails canonical validation during preprocessing before performing device actions

#### Scenario: Duplicate snapshot label is present
- **WHEN** multiple expanded `snapshot` commands anywhere in the flow use the same payload label
- **THEN** the system fails canonical validation before performing device actions

#### Scenario: Duplicate screenshot label is present
- **WHEN** multiple expanded `screenshot` commands anywhere in the flow use the same payload label
- **THEN** the system fails canonical validation before performing device actions

#### Scenario: Snapshot and screenshot labels match
- **WHEN** a `snapshot` command and a `screenshot` command use the same payload label after any macro expansion
- **THEN** the system accepts the flow if labels are otherwise valid and unique within their own command type

#### Scenario: UI coordinate is outside normalized range
- **WHEN** a UI command payload contains a percent coordinate less than `0` or greater than `100`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Top-level placeholder label is rejected at unprocessed validation
- **WHEN** a non-macro snapshot or screenshot command at the unprocessed top level uses a label containing `{` or `}` that no macro can substitute
- **THEN** the system fails unprocessed flow validation before expansion and before performing device actions

#### Scenario: Placeholder label inside macro body is accepted pre-expansion
- **WHEN** a repeat macro body snapshot or screenshot command uses a label containing `{<iter_var>}`
- **THEN** the unprocessed flow validates, and the label passes canonical validation only if the substituted result matches the label regex

### Requirement: Flow JSON supports repeat macro authoring
The system SHALL accept repeat macro entries inside unprocessed `AppFlow.commands` arrays. A repeat macro entry MUST have `macro` equal to `repeat` and a payload containing `iter_var`, `n_iter`, and `commands`. `iter_var` MUST match `^[A-Za-z_][A-Za-z0-9_]*$`. `n_iter` MUST be a strict non-negative integer with no upper bound. `commands` MUST be a list of concrete unprocessed commands and MUST NOT contain nested macros. `n_iter` of zero and an empty `commands` list SHALL be accepted and SHALL expand to no commands. The system SHALL expand repeat macros into concrete commands before canonical `Flow` validation and before performing device actions.

#### Scenario: Repeat macro expands in command order
- **WHEN** a flow contains a repeat macro with `n_iter` equal to `2` and two concrete commands in its payload
- **THEN** the system expands the macro into four concrete commands preserving the body order for each iteration before execution

#### Scenario: Loop variable substitutes string values
- **WHEN** a repeat macro has `iter_var` equal to `i` and a repeated command whose payload is a string contains `{i}`
- **THEN** the system replaces `{i}` with the 0-based iteration number in the canonical range `[0, n_iter-1]` in each expanded command, and string values inside payload structs such as `input_text.text` are not substituted

#### Scenario: N_iter of zero expands to nothing
- **WHEN** a repeat macro has `n_iter` equal to `0`
- **THEN** the macro expands to no commands without error

#### Scenario: Empty macro body expands to nothing
- **WHEN** a repeat macro has an empty `commands` list
- **THEN** the macro expands to no commands without error

#### Scenario: String substitution does not coerce numeric fields
- **WHEN** a repeat macro body command carries a string loop variable token in a numeric payload field such as `wait.payload`
- **THEN** macro body validation rejects the unprocessed flow before expansion because body commands reuse canonical strict payload types and strings are never coerced into numbers

#### Scenario: Macro body without loop variable can fail duplicate label validation
- **WHEN** a repeat macro repeats a `snapshot` command whose payload label does not include the loop variable and expansion creates duplicate snapshot labels
- **THEN** canonical `Flow` validation rejects the expanded flow before performing device actions

#### Scenario: Nested macros are rejected
- **WHEN** a repeat macro payload contains a repeat macro in its `commands` list
- **THEN** the system fails validation before performing device actions

#### Scenario: Invalid repeat macro payload fails validation
- **WHEN** a repeat macro has invalid `iter_var`, invalid `n_iter`, or missing payload fields
- **THEN** the system fails validation before performing device actions

### Requirement: Text command payload is a non-empty string
The system SHALL accept `text` commands only with a non-empty string payload and SHALL delegate the string to the device layer. A `text` command payload that is a struct such as `{ "text": "..." }` SHALL fail schema validation before any device action. The `key` command payload remains a struct that SHALL specify exactly one named key and is not substituted by macros.

#### Scenario: Text payload is a string
- **WHEN** a `text` command payload is a non-empty string
- **THEN** the system accepts the command and delegates the text to the device layer

#### Scenario: Text payload is a struct
- **WHEN** a `text` command payload is a struct such as `{ "text": "hello" }`
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Text payload substitutes in macro bodies
- **WHEN** a repeat macro body `text` command carries `{<iter_var>}` in its payload
- **THEN** expansion replaces the token with the 0-based iteration number in each expanded command

### Requirement: AppFlow launch stores label identity and PID metadata
The system SHALL launch each `AppFlow` once when that flow begins, resolve the launched application's current PID immediately, and record app label as the logical identity with PID, bundle, and ability metadata. The system SHALL retain every launched entry for output metadata and SHALL separately track entries eligible for snapshot collection. Multiple unique app labels MAY resolve to the same PID when the device foregrounds an already running process. When an `AppFlow` requests termination, the system SHALL remove that flow's launched label from future snapshot collection while retaining its launch metadata, then request bundle termination after that AppFlow finishes command execution.

#### Scenario: PID resolves after launch
- **WHEN** an `AppFlow` launch succeeds and a PID is resolved
- **THEN** the system records the unique app label and associated PID, bundle, and ability metadata as a launched entry that is eligible for snapshot collection

#### Scenario: PID cannot be resolved after launch
- **WHEN** an `AppFlow` launch completes but no PID can be resolved
- **THEN** the system fails the benchmark before executing that flow's commands

#### Scenario: Duplicate labels are present
- **WHEN** multiple `AppFlow` entries use the same label
- **THEN** the system rejects the flow during validation before launching applications

#### Scenario: Duplicate bundle and ability are present
- **WHEN** multiple `AppFlow` entries use the same bundle and ability with different labels
- **THEN** the system allows them and associates each label with the current PID resolved after its launch

#### Scenario: AppFlow termination is requested
- **WHEN** an `AppFlow` has `terminate` equal to `True`
- **THEN** the runner removes that flow's label from future snapshot collection and requests bundle termination after AppFlow command execution

#### Scenario: Terminated AppFlow metadata is preserved
- **WHEN** an `AppFlow` with `terminate` equal to `True` has launched successfully
- **THEN** its PID, label, bundle, and ability remain in output app metadata in launch order

#### Scenario: AppFlow termination is not requested
- **WHEN** an `AppFlow` has `terminate` equal to `False`
- **THEN** the runner does not request bundle termination for that AppFlow and keeps its label eligible for later snapshots

#### Scenario: Requested AppFlow termination fails
- **WHEN** an `AppFlow` has `terminate` equal to `True` and bundle termination fails
- **THEN** the system fails the benchmark

#### Scenario: AppFlow and requested termination both fail
- **WHEN** an `AppFlow` fails and requested bundle termination also fails during post-flow handling
- **THEN** the runner preserves the original AppFlow failure and SHOULD include the termination failure in a combined error when practical

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
- **THEN** the system records one timestamp and ordered expected files from the eligible active-apps list before liveness checks, excludes explicitly terminated labels, fails if any expected PID is missing, and immediately queues every successful capture for later receiving

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

### Requirement: Backend command support is validated before execution
The public `execute_flow` entry SHALL check every canonical command immediately after flow revalidation and before evidence-store creation or reservation or device preparation. It SHALL report the first unsupported command. The shared CLI collection helper SHALL preflight support before writing run provenance where practical, while the public API SHALL retain its own validation for direct callers.

#### Scenario: Device rejects an unsupported command
- **WHEN** a validated flow contains a command that the selected device reports unsupported
- **THEN** `execute_flow` fails before evidence creation or device actions

### Requirement: Benchmark execution caches screen bounds before app flows
The system SHALL read root screen bounds from the device before executing any `AppFlow` and store those bounds in the execution context for normalized coordinate conversion.

#### Scenario: Screen bounds are read successfully
- **WHEN** the benchmark begins execution after flow validation
- **THEN** the system reads device layout, parses root bounds, stores them in execution context, and only then launches the first `AppFlow`

#### Scenario: Screen bounds cannot be read
- **WHEN** device layout dumping fails before any `AppFlow` starts
- **THEN** the system fails the benchmark before launching any application

#### Scenario: Screen bounds are malformed
- **WHEN** the root layout does not contain parseable bounds
- **THEN** the system fails the benchmark before launching any application

#### Scenario: Normalized coordinate is converted
- **WHEN** a UI command requests coordinate conversion for `x_pct` and `y_pct`
- **THEN** the system converts the percent coordinate using cached root bounds and clamps the result to a positive in-bounds pixel coordinate

### Requirement: Benchmark execution prepares device environment before AppFlows
The benchmark runner SHALL prepare the device environment before launching any `AppFlow`. When reboot is enabled, it SHALL reboot the device, wait for selected-backend availability and boot completion, wake the device, perform an upward directional fling, and send Back. Regardless of reboot setting, it SHALL disable screen timeout before AppFlows. When `device_log` is enabled, it SHALL configure the selected backend's device logging before health verification and remote setup. It SHALL verify device health before remote output setup, then create remote output and prepare execution context before starting any enabled run-wide log stream. After startup validation and enabled stream startup, it SHALL launch AppFlows. This change SHALL preserve existing preparation ordering, timeouts, and health policy; backend-neutral terminology SHALL not imply a new device operation.

#### Scenario: Reboot is enabled
- **WHEN** benchmark options have `reboot` equal to `True`
- **THEN** the runner reboots, waits for selected-backend availability and boot completion, performs the post-reboot wake routine, configures enabled device logging, verifies health, creates remote output, reads screen bounds, and starts enabled streaming before launching `AppFlow`s

#### Scenario: Reboot is disabled
- **WHEN** benchmark options have `reboot` equal to `False`
- **THEN** the runner skips reboot, availability and boot waits, and post-reboot wake routine while still configuring enabled logging, verifying health, creating remote output, reading screen bounds, and starting enabled streaming before `AppFlow`s

#### Scenario: Post-reboot wake routine runs
- **WHEN** reboot preparation reaches boot completion
- **THEN** the runner wakes the device, performs an upward directional fling, and sends Back before disabling screen timeout

#### Scenario: Device health is acceptable
- **WHEN** benchmark execution prepares the device environment and device health reports battery capacity at least 30% and every readable nonzero thermal zone at or below 80000 millidegrees Celsius
- **THEN** the runner continues preparation and may launch AppFlows

#### Scenario: Battery capacity is too low
- **WHEN** benchmark execution prepares the device environment and device health reports battery capacity below 30%
- **THEN** the runner rejects startup before starting any enabled log stream, reading screen bounds, or launching `AppFlow`s

#### Scenario: Thermal zone is too hot
- **WHEN** benchmark execution prepares the device environment and any readable nonzero thermal zone reports a temperature above 80000 millidegrees Celsius
- **THEN** the runner rejects startup before starting any enabled log stream, reading screen bounds, or launching `AppFlow`s

#### Scenario: Screen timeout is disabled
- **WHEN** benchmark execution prepares the device environment
- **THEN** the runner disables screen timeout before health verification and `AppFlow`s independent of reboot and log options

#### Scenario: Device logging is enabled during preparation
- **WHEN** benchmark options have `device_log` equal to `True`
- **THEN** the runner configures the selected backend's logging before remote output setup and starts streaming after remote setup, health verification, and execution context preparation

### Requirement: Benchmark execution captures run-wide hilog
Execution SHALL collect one backend device-log stream per execution when `device_log` is true, using HDC hilog or ABD logcat with the existing lifecycle and fixed evidence path `logs/log.log`. It SHALL not clear or dump logs per `AppFlow` or delay requested termination for a per-app dump. Stream shutdown and pending receives SHALL still be attempted after execution failures. Disabled device logging SHALL skip configuration, start, stop, tracking, and receive. Memmem application logs SHALL remain separate.

#### Scenario: Enabled execution succeeds
- **WHEN** all `AppFlow`s succeed with device logs enabled
- **THEN** one log artifact is received at `logs/log.log` after stream shutdown

#### Scenario: Flow and shutdown both fail
- **WHEN** flow execution fails and stream shutdown also fails
- **THEN** the primary flow error is preserved with shutdown context and pending receives are still attempted

#### Scenario: Device logging is disabled
- **WHEN** `device_log` is false
- **THEN** no log lifecycle operations occur and no log evidence is required

#### Scenario: Termination does not wait for per-AppFlow dump
- **WHEN** an `AppFlow` requests termination with device logging enabled
- **THEN** termination proceeds after its commands without a per-AppFlow dump

### Requirement: Key command accepts named keys only
The system SHALL accept `key` commands only when the payload specifies one named key from `Home`, `Back`, or `Power`.

#### Scenario: Named key is valid
- **WHEN** a `key` command payload contains `key` equal to `Home`, `Back`, or `Power`
- **THEN** the system accepts the command and delegates the named key to the device layer

#### Scenario: Named key is invalid
- **WHEN** a `key` command payload contains any other key value
- **THEN** the system fails schema validation before performing device actions

#### Scenario: Numeric key codes are provided
- **WHEN** a `key` command payload contains numeric key codes or key combinations
- **THEN** the system fails schema validation before performing device actions

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

### Requirement: Offline view CLI creates a new independently named view
The system SHALL provide `create_view.py DIR1 [DIR2 ...] --out-dir PARENT [--smaps-filter REGEX]` with one or more positional exact evidence roots in argument order. It SHALL use compiled `.*` by default, reject invalid regex at parse time, accept an existing output parent, and create only a new `PARENT/view/view-YYYYMMDD_HHMMSS_microseconds` through `create_view`. Each invocation SHALL choose a fresh host timestamp; collision SHALL fail without overwrite. It SHALL require no `.env`, device executable, `flow.json`, prior view, iteration discovery, or backend options. `summarize.py` SHALL be removed, not retained as a legacy redirect.

#### Scenario: User generates another view
- **WHEN** an existing parent contains earlier views and valid evidence paths are explicitly supplied
- **THEN** a new timestamped view is created without changing earlier views or evidence

#### Scenario: Exact generated destination collides
- **WHEN** the generated view destination already exists
- **THEN** creation fails rather than merging, deleting, or replacing it
