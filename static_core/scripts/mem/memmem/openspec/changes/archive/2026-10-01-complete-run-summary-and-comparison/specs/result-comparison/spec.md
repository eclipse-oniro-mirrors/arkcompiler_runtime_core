# Spec Delta

## Purpose

Defines reproducible comparison of compatible memory benchmark result directories so regressions and improvements can be inspected without rerunning benchmarks.

## ADDED Requirements

### Requirement: Result comparison CLI compares a baseline with multiple runs
The system SHALL provide a host-side comparison CLI that accepts one baseline run-output directory, one or more comparison run-output directories, and a new comparison output directory. Each input MAY be a single-run output directory or a repeated-run root whose `summary/` directory contains aggregate reports. The CLI SHALL compare filtered aggregate reports when present and otherwise compare unfiltered summary reports.

#### Scenario: Multiple comparison runs are requested
- **WHEN** the user supplies one baseline and multiple comparison directories
- **THEN** the system writes one comparison value and percentage-delta column pair per memory metric for each comparison directory

#### Scenario: Repeated-run root is supplied
- **WHEN** an input directory contains a `summary/` directory with aggregate reports
- **THEN** the system compares reports from that `summary/` directory

#### Scenario: Single-run output is supplied
- **WHEN** an input directory contains reports directly at its root
- **THEN** the system compares those root reports

### Requirement: Result comparison preserves report identities and coverage
The comparison output SHALL contain a summary CSV keyed by `app_label` and `snapshot_label` and breakdown CSVs keyed by tag. Every input summary SHALL have the same ordered `(app_label, snapshot_label)` rows as the baseline. Comparison breakdowns SHALL include the union of tags found in the baseline and all comparison inputs, leaving a value and percentage delta empty when its source report has no value for that tag. The system SHALL reject missing, malformed, non-finite, or incompatible report input with an error identifying the affected input.

#### Scenario: Summary layouts differ
- **WHEN** a comparison input has different ordered app-label and snapshot-label pairs from the baseline
- **THEN** the CLI fails before publishing comparison output

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
The comparison CLI SHALL reject an existing output directory. It SHALL validate every input summary and breakdown report before creating comparison output, and a validation or generation failure SHALL leave no new partial comparison output directory.

#### Scenario: Output directory already exists
- **WHEN** the requested comparison output directory already exists
- **THEN** the CLI fails without modifying it

#### Scenario: Input validation fails
- **WHEN** an input report is missing, malformed, or incompatible
- **THEN** the CLI fails before creating the comparison output directory

#### Scenario: Comparison succeeds
- **WHEN** every requested input is compatible and valid
- **THEN** the CLI creates the output directory with its complete summary and breakdown comparison CSVs
