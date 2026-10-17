# Design

## Context

See `proposal.md` for motivation. `lib.run()` currently executes repeated iterations and calls `average_reports()` only after every iteration succeeds. The reviewed `summarize.py` collects every `iteration_N` directory and relies on the operator to remove a failed directory. It also writes directly into `summary/`. The reviewed `compare.py` supports repeated `--change` arguments but writes its output before it has parsed every breakdown and omits tags that occur only in a comparison input.

## Goals / Non-Goals

**Goals:**
- Reliably distinguish a completed repeated iteration from a directory left by a failed iteration.
- Recover the standard aggregate-report layout without publishing a partial or stale replacement.
- Compare one baseline with multiple run outputs while retaining all observed breakdown tags.
- Keep both utilities local, dependency-free Python CLIs and document their input and output contracts.

**Non-Goals:**
- Resume failed benchmark device execution.
- Change summary statistics, outlier selection, or raw benchmark evidence formats.
- Compare flows with different summary row identities.
- Produce graphical regression dashboards or threshold-based pass/fail decisions.

## Decisions

### Use per-iteration summary reports as completion evidence

Recovery scans numeric iteration directories in numeric order and passes only directories containing `summary.csv` to the existing aggregation path; it emits a warning for each directory without that report.

This keeps recovery compatible with existing repeated-run output and avoids changing `src/` modules. A heuristic that drops the highest iteration was rejected because the final directory can be complete when aggregation itself fails.

### Publish recovered summaries through a staging directory

Make `summarize.py` move any existing `summary/` aside before calling the existing aggregate-report generator, then restore it if recovery fails. Normal repeated-run aggregation remains unchanged.

Directly overwriting files in `summary/` was rejected because it can retain stale plots or breakdowns and leaves a misleading partial result on failure.

### Fully load comparison input before output creation

Parse and validate every selected summary and breakdown CSV into in-memory report structures before reserving the comparison output directory. Inputs resolve to `<run>/summary/` when present, otherwise to the supplied directory. Select filtered aggregate data when `summary_filtered.csv` is available; otherwise use the unfiltered summary.

The current lazy breakdown reads after output creation were rejected because malformed late input leaves partial evidence. No external CSV or statistics dependency is required.

### Make comparison coverage explicit and deterministic

Require every comparison summary to have the baseline's ordered `(app_label, snapshot_label)` identities. For each compatible breakdown file, emit tags in baseline order followed by first appearance across comparison inputs. Store one base value plus one value/percentage pair per comparison input in CLI order. A percentage is empty if the baseline is zero or that report lacks the tag.

Rejecting different breakdown tag sets was rejected because new or removed mappings are meaningful regressions. The current base-only tag output was rejected because it hides comparison-only mappings.

## Risks / Trade-offs

- [A partial iteration can contain a `summary.csv`] → Recovery uses the same report-presence rule as aggregate reporting and warns only for directories without that report.
- [Filesystem replacement fails during recovery] → Keep backup/restore handling in `summarize.py`; surface an error and do not claim a new summary was published.
- [Large comparison reports consume memory] → The inputs are bounded CSV report artifacts; parse once before output creation to guarantee atomic publication. Revisit streaming only if report sizes become a demonstrated issue.
- [Filtered and unfiltered inputs use different metric column names] → Normalize each report independently while retaining its selected report mode before assembling comparison rows.

## Migration Plan

1. Add safe summary backup and recovery around the existing aggregate-report calculations.
3. Complete comparison validation and union-tag output behavior.
4. Document the marker limitation for pre-change outputs and the exact CLI invocation patterns.
5. Roll back by removing the new utilities and marker handling; existing raw iteration evidence remains untouched.
