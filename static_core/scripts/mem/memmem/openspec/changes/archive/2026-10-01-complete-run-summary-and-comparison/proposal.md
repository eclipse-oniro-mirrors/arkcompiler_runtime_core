# Proposal

## Why

Repeated benchmarks that stop mid-run retain usable iteration evidence but cannot safely produce their aggregate reports without manual filesystem edits. Engineers also need a supported, reproducible way to compare the resulting reports across several benchmark output directories.

## What Changes

- Provide a recovery CLI that creates `summary/` from complete iterations of an interrupted repeated run without requiring manual deletion of a failed iteration.
- Make recovered aggregate output safe to regenerate and prevent stale or partial summary artifacts from being mistaken for current results.
- Provide a comparison CLI that accepts one baseline and multiple comparison run directories and writes per-metric absolute and percentage deltas for summary and breakdown reports.
- Document both utilities and add focused unit and CLI tests for success, malformed input, incomplete iterations, and output-directory failures.

## Capabilities

### New Capabilities
- `result-comparison`: Compares compatible benchmark result directories against a baseline and emits CSV comparison evidence.

### Modified Capabilities
- `result-evidence`: Allows interrupted repeated-run evidence to be safely aggregated into the standard summary layout.

## Impact

- Affects `summarize.py`, `compare.py`, report aggregation, tests, `Makefile`, and `README.md`.
- Adds two local host-side CLI workflows; it does not change device execution, raw benchmark evidence, or existing `run.py` result formats.
