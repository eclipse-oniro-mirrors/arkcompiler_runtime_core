# Testing support delta

## REMOVED Requirements

### Requirement: Programmatic interface tests cover public facade behavior
**Reason**: lib.run and combined evidence/report execution are removed.
**Migration**: Use separated-facade tests below while retaining builder, preprocessing, recorder and public typing regression coverage.

### Requirement: Averaged report tests cover statistics and robustness
**Reason**: Intermediate CSV inputs, dual output summaries and first-seen ordering are removed.
**Migration**: Test direct parsed-evidence analysis, strict readers and metadata ordering below.

### Requirement: Smaps tag filter and renamed artifact layout are covered by tests
**Reason**: Default None, old basenames and raw report paths no longer apply.
**Migration**: Test compiled defaults, manifests, shortened paths and view equivalence below.

## MODIFIED Requirements

### Requirement: SwapPss parsing and report output are covered by tests
Tests SHALL preserve SwapPss aggregation, zero default when absent, and swap_pss_kb in total/tag profiles. They SHALL verify metric-major aggregate statistic columns for swap_pss_kb and per-app `<app>-swap_pss.svg` output, with metric enumeration derived from the shared metrics definition for every source count.

#### Scenario: SwapPss is supplied or absent
- **WHEN** parser fixtures include or omit SwapPss
- **THEN** tests verify the summed or zero profile values respectively

#### Scenario: View exposes SwapPss
- **WHEN** single-source and multi-source views are generated
- **THEN** tests verify swap_pss_kb aggregate columns and the per-app SVG without raw-schema assumptions

#### Scenario: Parser aggregates SwapPss
- **WHEN** mappings include SwapPss
- **THEN** tests verify its summed total and per-tag values

#### Scenario: Parser treats missing SwapPss as zero
- **WHEN** mappings omit SwapPss
- **THEN** tests verify zero contributions for that metric

#### Scenario: Plots and CSVs include swap_pss
- **WHEN** views are generated
- **THEN** tests verify swap_pss_kb statistic columns and `<app>-swap_pss.svg` using shared metric enumeration

## ADDED Requirements

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
