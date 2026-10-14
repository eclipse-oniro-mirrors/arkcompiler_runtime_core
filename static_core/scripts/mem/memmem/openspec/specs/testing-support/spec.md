# testing-support Specification

## Purpose
Defines shared test support fakes and dependency-injection test seams used by framework tests.

## Requirements

### Requirement: Shared fake HDC supports command logging and configured results
The test suite SHALL provide a reusable fake HDC object in `test/mock/hdc.py` that records every `run`, `shell`, and `shell_raw` invocation and returns configured `HdcResult` values without executing external commands. The fake HDC SHALL support the same timeout parameter as the production HDC wrapper and MAY be configured with `timeout_after` to return `HdcResult(127, "", f"memmem: TIMEOUT {cmd}")` when a provided timeout is too small.

#### Scenario: Default HDC result is returned
- **WHEN** a test calls `run`, `shell`, or `shell_raw` with no matching configured response
- **THEN** the fake HDC returns its configured default `HdcResult`

#### Scenario: Raw run arguments are recorded
- **WHEN** a test calls `run` with HDC arguments
- **THEN** the fake HDC records exactly those arguments without adding a `run` marker

#### Scenario: Exact HDC response is returned
- **WHEN** a test calls `run`, `shell`, or `shell_raw` with arguments matching a configured response key
- **THEN** the fake HDC returns the matching `HdcResult` and records the call

#### Scenario: Prefix HDC response is returned
- **WHEN** a test calls `run`, `shell`, or `shell_raw` with arguments matching a configured response prefix and no exact response matches
- **THEN** the fake HDC returns the matching prefix `HdcResult` and records the call

#### Scenario: Fake HDC timeout is simulated
- **WHEN** a fake HDC is configured with `timeout_after` and a command is called with a timeout smaller than that threshold
- **THEN** the fake HDC returns `HdcResult(127, "", f"memmem: TIMEOUT {cmd}")`

### Requirement: Shared fake device models benchmark-relevant device state
The test suite SHALL provide a reusable fake device object in `test/mock/device.py` with configurable screen bounds, process state, invalid bundles, remote directories, remote files, health observations, failure flags for device operations used by benchmark execution, and fake generic child process handles for run-wide hilog streams. The fake device SHALL support reboot, boot readiness, wakeup, screen-timeout disabling, hilog configuration, hilog stream start/stop through generic child processes, app termination, and screenshot capture.

#### Scenario: Valid app launch creates process state
- **WHEN** a valid bundle is launched for the first time
- **THEN** the fake device stores a bundle-to-PID mapping that can be resolved later

#### Scenario: Repeated app launch reuses process state
- **WHEN** a bundle that already has process state is launched again
- **THEN** the fake device keeps the existing PID for that bundle

#### Scenario: Invalid app launch does not create process state
- **WHEN** a bundle configured as invalid is launched
- **THEN** the fake device does not add process state for that bundle and PID resolution fails

#### Scenario: Screen bounds are returned from configured state
- **WHEN** benchmark execution asks for screen bounds and no screen error is configured
- **THEN** the fake device returns its configured `ScreenBounds`

#### Scenario: Screen bounds failure is configured
- **WHEN** benchmark execution asks for screen bounds and a screen error is configured
- **THEN** the fake device raises that error

#### Scenario: Remote directory is created
- **WHEN** benchmark execution creates a remote directory and directory creation is not configured to fail
- **THEN** the fake device records that path in its remote directory set

#### Scenario: Smaps capture requires parent directory
- **WHEN** benchmark execution captures smaps to a remote path whose parent directory is not recorded
- **THEN** the fake device reports smaps capture failure

#### Scenario: Smaps capture writes remote file
- **WHEN** benchmark execution captures smaps for an existing PID to a path whose parent directory exists
- **THEN** the fake device stores smaps content in its remote files map

#### Scenario: Screenshot capture requires parent directory
- **WHEN** benchmark execution captures a screenshot to a remote path whose parent directory is not recorded
- **THEN** the fake device reports screenshot capture failure

#### Scenario: Screenshot capture writes remote file
- **WHEN** benchmark execution captures a screenshot to a path whose parent directory exists
- **THEN** the fake device stores fake PNG content in its remote files map

#### Scenario: Remote file receive writes local file
- **WHEN** benchmark execution receives a remote file that exists in fake device state
- **THEN** the fake device writes the file content to the requested local path and returns a successful `HdcResult`

#### Scenario: Remote directory removal clears subtree
- **WHEN** benchmark execution removes a remote directory and removal is not configured to fail
- **THEN** the fake device removes matching remote directories and files under that path

#### Scenario: Fake device reboots
- **WHEN** benchmark execution requests reboot and reboot is not configured to fail
- **THEN** the fake device records reboot state

#### Scenario: Fake device wakes up
- **WHEN** benchmark execution requests wakeup and wakeup is not configured to fail
- **THEN** the fake device records wakeup state

#### Scenario: Fake device reports health
- **WHEN** benchmark execution requests device health and health reporting is not configured to fail
- **THEN** the fake device returns its configured battery capacity and thermal zone observations

#### Scenario: Fake device health fails
- **WHEN** benchmark execution requests device health and health reporting is configured to fail
- **THEN** the fake device raises a device health failure

#### Scenario: Fake device terminates app
- **WHEN** benchmark execution requests app termination and termination is not configured to fail
- **THEN** the fake device removes process state for that bundle

#### Scenario: Fake device termination fails
- **WHEN** benchmark execution requests app termination and termination is configured to fail
- **THEN** the fake device raises a termination failure

#### Scenario: Fake device starts hilog child process
- **WHEN** test code requests run-wide hilog streaming to a remote path on a fake device instance and hilog streaming is not configured to fail
- **THEN** that fake device instance returns a fake generic child process handle and stores fake log content in its remote files map at that path

#### Scenario: Fake hilog child process stops
- **WHEN** benchmark finalization stops a hilog child process returned by the fake device
- **THEN** the process exits without requiring multiprocessing shared memory or Unix process-group signaling

#### Scenario: Fake device hilog stream fails
- **WHEN** benchmark execution requests run-wide hilog streaming to a remote path and hilog streaming is configured to fail
- **THEN** the fake device raises a hilog streaming failure

### Requirement: Schema regression coverage for wait payload numerics and flat text payload
The test suite SHALL verify that `wait` payload validation accepts non-negative integer and float values, rejects negative and non-numeric values, and that the flat `text` action rejects the legacy struct payload form.

#### Scenario: Wait payload accepts non-negative numeric values
- **WHEN** tests validate `wait` commands with non-negative integer and float payloads
- **THEN** the commands load and validate successfully

#### Scenario: Wait payload rejects negative and non-numeric values
- **WHEN** tests validate `wait` commands with negative or non-numeric payloads
- **THEN** schema validation fails

#### Scenario: Flat text payload rejects legacy struct form
- **WHEN** tests validate a `text` command whose payload is a struct such as `{"text": "..."}` instead of a flat string
- **THEN** schema validation fails

### Requirement: SwapPss parsing and report output are covered by tests
Tests SHALL preserve SwapPss aggregation, zero default when absent, and swap_pss_kb in total/tag profiles. They SHALL verify metric-major aggregate statistic columns for swap_pss_kb and per-app `<app>-swap_pss.svg` output, with metric enumeration derived from the shared metrics definition for every source count.

#### Scenario: Parser aggregates SwapPss
- **WHEN** tests parse smaps text whose mappings contain `SwapPss` values
- **THEN** the returned profiles expose summed `swap_pss_kb` values in totals and per-tag breakdowns

#### Scenario: Parser treats missing SwapPss as zero
- **WHEN** tests parse smaps text without any `SwapPss` field
- **THEN** the returned profiles expose `swap_pss_kb` equal to zero

#### Scenario: View exposes SwapPss
- **WHEN** single-source and multi-source views are generated
- **THEN** tests verify swap_pss_kb aggregate columns and the per-app SVG without raw-schema assumptions

#### Scenario: Plots and CSVs include swap_pss
- **WHEN** views are generated
- **THEN** tests verify swap_pss_kb statistic columns and `<app>-swap_pss.svg` using shared metric enumeration

### Requirement: Separated APIs and CLIs have regression coverage
Tests SHALL verify execute_flow/create_view signatures, normalized Path returns, public exports, canonical-flow revalidation, backend support preflight, caller-owned logging, no reports from execution, no device/config access from view creation, exclusive destinations, accepted existing CLI parents, flow persistence once, iteration_0 for one repeat, collect-before-render ordering, one shared timestamp, and fail-fast retention. All CLI parsers SHALL have no-abbreviation tests. Typed public-script fixtures SHALL use lib plus standard-library types, not internal parsed/store types. Existing canonical/unprocessed builders, macros, description, flat text/wait schema, unknown-key rejection, record parsing/conversion/identity ordering, unsupported recorder events, device helpers and public type boundaries SHALL retain regression coverage after replacing run call sites.

#### Scenario: Library execution completes
- **WHEN** a fake-device execution succeeds
- **THEN** tests verify structural evidence validity, normalized return value, unchanged caller logger and no report-analysis call

#### Scenario: Combined CLI ordering is observed
- **WHEN** run.py is exercised with multiple repeats
- **THEN** tests verify all execution calls precede every view call, views use explicit returned evidence paths and a common timestamp, and a failure stops the remaining phase

#### Scenario: Existing output is supplied
- **WHEN** a CLI parent exists but API dst is new, or the exact API dst exists
- **THEN** tests verify parent reuse in the first case and unchanged exact destination rejection in the second

#### Scenario: CLI options and typing are checked
- **WHEN** parsers and typed facade fixtures are exercised
- **THEN** tests reject abbreviations, removed options, invalid regex/repeats and old lib.run usage while preserving builder/preprocessor/recorder semantics

### Requirement: Expected artifact and reader tests cover failure boundaries
Tests SHALL cover declaration-before-capture, fatal unresolved launch/stored PIDs, explicit termination exclusions, nonempty file arrays, eager empty metadata, successful capture-prefix receiving after later failure, all pending receives despite one failure, acquired-resource cleanup, primary/secondary error preservation and cwd restoration. Shared fake device behavior SHALL use logs/log.log and new snapshot paths without implicit legacy-file fabrication. Reader tests SHALL cover missing directories/metadata/declared files, wrong extensions/parents, unknown apps, duplicate labels/files, invalid timestamps, legacy rejection, ignored extra files and ignored logs. Tests SHALL distinguish structural validation from whole-flow/content-integrity verification.

#### Scenario: PID disappears after earlier captures
- **WHEN** a snapshot has a successful prefix followed by a missing PID
- **THEN** tests verify execution raises, declarations remain, the prefix is received and no later commands run

#### Scenario: Declared and undeclared artifacts differ
- **WHEN** fixtures omit a declared file or add an undeclared file
- **THEN** tests verify missing declarations fail while extras are ignored

#### Scenario: Screenshot and log differences are present
- **WHEN** individually valid evidence sources have different screenshots or log settings
- **THEN** tests verify those differences do not defeat evidence equivalence while missing declared screenshots still fail local validity

### Requirement: Equivalence and ordered output have focused coverage
Tests SHALL exercise the reusable boolean evidence/view equivalence APIs. Evidence equivalence SHALL ignore PID/timestamps/app-array order but reject bundle/ability, ordered snapshot-label or ordered file-array changes. View equivalence SHALL compare regex source/flags and ordered final breakdown declarations, ignoring timestamps, values, sources/counts and outliers. Reader tests SHALL verify summary rows exactly follow their own metadata before equivalence. Filenames SHALL use `<snapshot>/<app>.smaps` and `<snapshot>/<app>.csv`, including hyphen/underscore labels.

#### Scenario: File members are reordered
- **WHEN** equivalent-looking declarations have the same members in a different array order
- **THEN** tests verify equivalence is false rather than set-based

#### Scenario: Early filtered row is absent in first source
- **WHEN** an earlier declared pair first contributes in a later retained source
- **THEN** tests verify summary and breakdown manifests use declaration order, not first-seen contributing order

#### Scenario: View values change
- **WHEN** source paths/counts, timestamps or numeric values change but filters and declarations match
- **THEN** tests verify view equivalence remains true

### Requirement: Uniform statistics and filtering retain established behavior
Tests SHALL cover every metric/statistic/header, one-source uniformity, matching-zero versus no-match distinction, anonymous/start-anchored regex semantics, compiled default and flag propagation, rejected None/bytes patterns, per-call path/pattern warnings, pair/tag sample populations, no zero filling, sample std, zero geomean, deterministic outlier quota/retention/ties, zero-median cells, empty samples in the population, whole-source removal, first-retained timing, final no-match/outlier coverage, normalized full-path discarded audit, and no pretrim/raw summary output. Shared named app/snapshot identities SHALL avoid tuple-order mistakes. Valid empty views SHALL be round-trip readable with eager directories and empty breakdown artifact arrays.

#### Scenario: Valid empty samples are supplied
- **WHEN** multiple valid sources have no matching mappings
- **THEN** tests verify score-zero participation in quota selection, header-only summary and no plots without treating sources as invalid

#### Scenario: First supplied source is discarded
- **WHEN** outlier selection removes the first input
- **THEN** tests verify reference timestamps come from the first retained source and plot origins are first plotted points per app

#### Scenario: Tags have differing populations
- **WHEN** a tag is absent from some retained sources
- **THEN** tests verify tag counts/statistics use present values only and do not require sums of tag means to equal summary means

### Requirement: Standalone view reading and comparison are strictly tested
Tests SHALL verify generated-view reader round trips, exact aggregate headers/widths, positive integer counts, integer nonnegative min/max, finite nonnegative other statistics, duplicate summary/tag rejection, nonempty declared breakdowns, manifest-to-summary ordering and ignored extras. Comparison SHALL be tested without source evidence available, without summary_filtered.csv, with metadata filter/flag incompatibility, ordered layout incompatibility, tag unions/blanks, zero baselines and stable change order. Failure tests SHALL distinguish unchanged existing destinations from retained new partial outputs. Comparison output SHALL not be accepted as a view.

#### Scenario: Non-median data is invalid
- **WHEN** a fixture has valid medians but invalid other required statistics or wrong row width
- **THEN** the view reader rejects it before comparison

#### Scenario: Evidence is unavailable
- **WHEN** source evidence is removed or inaccessible after valid views exist
- **THEN** comparison succeeds without attempting to open source paths

#### Scenario: View or comparison writing fails
- **WHEN** generation fails after creating its new destination
- **THEN** tests verify partial output remains and prior destinations/evidence remain untouched

### Requirement: Lexical path handling is covered without symlink resolution
Tests SHALL cover normalize/absolutize, invocation-relative flow/log/backend/output paths, normalized API return values, metadata/audit path consistency, lexical duplicate-source rejection and preserved symlink spelling. Tests SHALL not require physical inode deduplication, source/destination ancestry checks, devices, GUI services or legacy migration.

#### Scenario: Lexical source aliases are supplied
- **WHEN** the same source is spelled with collapsible dot components
- **THEN** tests verify duplicate rejection and normalized absolute provenance

#### Scenario: Output destination does not exist
- **WHEN** normalization is applied to a future destination
- **THEN** tests verify normalization succeeds without requiring it to exist or resolving symlinks

### Requirement: Device command translation tests use fake HDC logging
Tests that assert concrete HDC command arguments SHALL use the real device layer with fake HDC call logging rather than fake device method logging.

#### Scenario: UI command translation is asserted
- **WHEN** a test validates the HDC command emitted for a UI input operation
- **THEN** the test uses real `Device` behavior and inspects `FakeHdc.calls`

#### Scenario: Termination command translation is asserted
- **WHEN** a test validates the HDC command emitted for app termination
- **THEN** the test uses real `Device` behavior and inspects `FakeHdc.calls`

#### Scenario: Screenshot command translation is asserted
- **WHEN** a test validates the HDC command emitted for screenshot capture
- **THEN** the test uses real `Device` behavior and inspects `FakeHdc.calls`

#### Scenario: Hilog stream command translation is asserted
- **WHEN** a test validates the HDC command emitted for run-wide hilog streaming
- **THEN** the test uses real `Device` behavior and inspects `FakeHdc.calls`

#### Scenario: Hilog stream process lifecycle is portable
- **WHEN** a test validates run-wide hilog stream process lifecycle behavior
- **THEN** the test does not require `os.setsid`, `os.killpg`, Unix signals, or multiprocessing process-group cleanup
