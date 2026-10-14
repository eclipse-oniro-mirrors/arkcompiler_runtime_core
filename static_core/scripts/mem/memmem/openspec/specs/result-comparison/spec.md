# result-comparison Specification

## Purpose

Defines reproducible comparison of compatible memory benchmark result directories so regressions and improvements can be inspected without rerunning benchmarks.

## Requirements

### Requirement: Result comparison CLI compares a baseline with multiple runs
The comparison CLI SHALL accept `--base` pointing to one concrete new-format view, repeated `--change` arguments pointing to concrete views in command-line order, and `--out-dir` for an exact new comparison destination. It SHALL load validated views and compare their aggregate median columns from `summary.csv` and declared breakdowns. It SHALL NOT autodiscover run roots, latest views, summary directories or legacy schemas, select `summary_filtered.csv`, regenerate reports, or reopen source evidence. Argument abbreviation SHALL be disabled and the CLI SHALL expose and independently own memmem logging options.

#### Scenario: New aggregate view is supplied
- **WHEN** each input has valid view metadata and aggregate `summary.csv` but no `summary_filtered.csv`
- **THEN** median-based comparison succeeds without raw-schema inference

#### Scenario: Run root is supplied
- **WHEN** an input is a run/evidence root rather than a concrete view
- **THEN** comparison rejects it rather than discovering a nested view

#### Scenario: Evidence paths are unavailable
- **WHEN** valid views refer to unavailable source evidence
- **THEN** comparison uses only view-local files and still succeeds

#### Scenario: Multiple comparison runs are requested
- **WHEN** one baseline view and multiple change views are supplied
- **THEN** output includes baseline values and each change's values/deltas in argument order

#### Scenario: Repeated-run root is supplied
- **WHEN** an input is a repeated-run root rather than a concrete view
- **THEN** comparison rejects it without nested-summary or latest-view discovery

#### Scenario: Single-run output is supplied
- **WHEN** an input is legacy single-run output rather than a new-format view
- **THEN** comparison rejects it without raw-summary fallback

### Requirement: Result comparison preserves report identities and coverage
Comparison SHALL use the single view-equivalence contract: exact regex source/flags and ordered breakdown artifact labels/files, ignoring timestamps, source paths/counts, values and outliers. Readers SHALL first verify each summary's exact identity sequence against its own metadata, making positional summary comparison valid without another cross-view layout policy. Comparison SHALL preserve baseline measurement order, change argument order, Decimal percentage arithmetic and original numeric text. Tag rows SHALL contain the union of input tags, leaving unavailable values/deltas blank and percentage against zero baseline blank. Matching labels SHALL be documented as caller assertions of semantic comparability; app identity SHALL not be inferred by opening evidence. Comparison output SHALL retain summary and `<snapshot>/<app>.csv` breakdown comparative columns and SHALL NOT be represented as a view.

#### Scenario: Numeric values differ but declarations match
- **WHEN** validated views have equivalent ordered declarations and filters but different values or source counts
- **THEN** they are comparable

#### Scenario: Declaration order or filter differs
- **WHEN** file/event order, regex text or effective flags differ
- **THEN** comparison rejects the views as incompatible before writing output

#### Scenario: Summary does not honor its own manifest
- **WHEN** one view's CSV rows are reordered or duplicated relative to metadata
- **THEN** its reader rejects it before metadata equivalence is evaluated

#### Scenario: A tag exists only in a comparison input
- **WHEN** a breakdown tag is absent from the baseline but present in one or more comparison inputs
- **THEN** the corresponding output row contains blank baseline percentage fields and the available comparison values

#### Scenario: A tag exists only in the baseline
- **WHEN** a breakdown tag is absent from a comparison input but present in the baseline
- **THEN** the corresponding comparison value and percentage fields are blank for that input

#### Scenario: Baseline value is zero
- **WHEN** a baseline metric value is zero
- **THEN** the comparison output preserves the numeric values and leaves that metric's percentage delta empty

### Requirement: Result comparison publishes complete output only
Comparison SHALL validate input views and compatibility before creating its destination, and SHALL reject an existing destination without modifying it. On successful generation it SHALL write all comparative CSVs. Once a new destination is created, a generation failure SHALL raise and leave partial output for debugging; no cleanup, backup, restoration, staging, or atomic-publication guarantee SHALL apply. The retained historical requirement title SHALL not be interpreted as an atomicity promise.

#### Scenario: Output directory already exists
- **WHEN** the requested comparison output directory already exists
- **THEN** the CLI fails without modifying it

#### Scenario: Input validation fails
- **WHEN** an input is missing, malformed, or incompatible
- **THEN** comparison fails before creating output

#### Scenario: Generation fails after writing starts
- **WHEN** writing a new comparison destination fails
- **THEN** comparison reports failure and leaves its partial files

#### Scenario: Comparison succeeds
- **WHEN** all inputs are valid and generation succeeds
- **THEN** the destination contains the complete comparative summary and breakdown CSVs
