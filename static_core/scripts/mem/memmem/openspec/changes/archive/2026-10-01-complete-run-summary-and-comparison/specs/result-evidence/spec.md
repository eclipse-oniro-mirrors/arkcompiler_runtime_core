# Spec Delta

## ADDED Requirements

### Requirement: Interrupted repeated runs can recover aggregate evidence
The system SHALL provide a host-side recovery CLI that accepts a repeated-run output directory, aggregates iteration directories that contain `summary.csv`, and writes the same standard aggregate reports under its `summary/` directory as a completed repeated run. Iteration directories without `summary.csv` SHALL be excluded with a warning, and the recovery CLI SHALL fail when no iteration summary is available.

#### Scenario: Completed iteration has a summary report
- **WHEN** a repeated-run iteration finishes benchmark execution and per-iteration report generation successfully
- **THEN** its iteration directory contains `summary.csv` for aggregate recovery

#### Scenario: Interrupted final iteration is excluded
- **WHEN** a repeated run stops after creating an iteration directory without `summary.csv` and one or more earlier iterations have summary reports
- **THEN** recovery aggregates only the iterations with summary reports without requiring the user to delete the incomplete directory

#### Scenario: Recovery writes standard aggregate reports
- **WHEN** recovery finds one or more complete iteration directories
- **THEN** it writes `summary.csv`, `summary_filtered.csv`, `outliers.csv`, averaged plots, and averaged breakdowns under `<out-dir>/summary/` with the same semantics as normal repeated-run aggregation

#### Scenario: No complete iteration is available
- **WHEN** recovery finds no iteration directory with `summary.csv`
- **THEN** it fails without publishing a new or partial aggregate summary

#### Scenario: Recovery replaces a prior summary safely
- **WHEN** recovery is rerun for an output directory that already contains `summary/`
- **THEN** it either replaces the complete summary as one result set or leaves the prior summary unchanged when recovery fails
