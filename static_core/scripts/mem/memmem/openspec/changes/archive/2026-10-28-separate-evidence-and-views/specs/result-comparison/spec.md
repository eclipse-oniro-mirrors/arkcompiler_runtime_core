# Result comparison delta

## MODIFIED Requirements

### Requirement: Result comparison CLI compares a baseline with multiple runs
The comparison CLI SHALL accept `--base` pointing to one concrete new-format view, repeated `--change` arguments pointing to concrete views in command-line order, and `--out-dir` for an exact new comparison destination. It SHALL load validated views and compare their aggregate median columns from summary.csv and declared breakdowns. It SHALL NOT autodiscover run roots, latest views, summary directories or legacy schemas, select summary_filtered.csv, regenerate reports, or reopen source evidence. Argument abbreviation SHALL be disabled and the CLI SHALL expose and independently own memmem logging options.

#### Scenario: New aggregate view is supplied
- **WHEN** each input has valid view metadata and aggregate summary.csv but no summary_filtered.csv
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

#### Scenario: Tag is missing on one side
- **WHEN** a tag appears in some breakdown inputs but not others
- **THEN** its available values are compared and missing values/deltas are blank rather than zero-filled

#### Scenario: Baseline is zero
- **WHEN** a metric baseline is zero
- **THEN** the comparison preserves numeric values and leaves the percentage cell blank

#### Scenario: Summary layouts differ
- **WHEN** a reader finds CSV identities disagreeing with metadata or validated view metadata declares different layouts
- **THEN** comparison fails before output creation

#### Scenario: A tag exists only in a comparison input
- **WHEN** a change breakdown has a tag absent from baseline
- **THEN** the union row preserves its change values with blank unavailable baseline/delta cells

#### Scenario: A tag exists only in the baseline
- **WHEN** a baseline tag is absent from a change
- **THEN** that change's value and delta cells are blank

#### Scenario: Baseline value is zero
- **WHEN** the baseline metric is zero
- **THEN** percentage output is blank while available numeric values remain

### Requirement: Result comparison publishes complete output only
Comparison SHALL validate input views and compatibility before creating its destination, and SHALL reject an existing destination without modifying it. On successful generation it SHALL write all comparative CSVs. Once a new destination is created, a generation failure SHALL raise and leave partial output for debugging; no cleanup, backup, restoration, staging, or atomic-publication guarantee SHALL apply. The retained historical requirement title SHALL not be interpreted as an atomicity promise.

#### Scenario: Existing output is rejected
- **WHEN** comparison dst exists
- **THEN** it fails without modifying it

#### Scenario: Input is invalid
- **WHEN** a required input is missing, malformed or incompatible
- **THEN** comparison fails before creating output

#### Scenario: Generation fails after writing starts
- **WHEN** writing a new comparison destination fails
- **THEN** comparison reports failure and leaves its partial files

#### Scenario: Output directory already exists
- **WHEN** comparison destination exists
- **THEN** it is rejected without modification

#### Scenario: Input validation fails
- **WHEN** an input is missing, malformed or incompatible
- **THEN** comparison fails before creating output

#### Scenario: Comparison succeeds
- **WHEN** all inputs are valid and generation succeeds
- **THEN** the destination contains the complete comparative summary and breakdown CSVs
