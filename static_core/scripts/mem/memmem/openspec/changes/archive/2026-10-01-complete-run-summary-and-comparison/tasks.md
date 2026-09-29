# Tasks

## 1. Safe Repeated-Run Recovery

- [x] 1.1 Keep repeated-run execution unchanged and use each iteration's `summary.csv` as recovery completion evidence; add tests covering available and missing per-iteration summaries.
- [x] 1.2 Safely replace `summary/` inside `summarize.py` without changing `src/` aggregate-report publication; add tests proving a failed recovery preserves a prior summary.
- [x] 1.3 Complete `summarize.py` to select summary-bearing iterations, warn for missing reports, reject runs with no summaries, and preserve logger cleanup; add focused unit and CLI tests.
- [x] 1.4 Document recovery CLI usage, summary-report eligibility, and recovery failure behavior in `README.md`; verify every documented command and path matches the parser.

## 2. Multi-Run Report Comparison

- [x] 2.1 Refactor `compare.py` to fully parse and validate all selected summary and breakdown inputs before reserving output; add tests for malformed, non-finite, missing, and incompatible reports leaving no output directory.
- [x] 2.2 Emit deterministic union-tag breakdown rows across the baseline and every comparison input, preserving blank unavailable values and zero-baseline percentage behavior; extend comparison tests for base-only and change-only tags.
- [x] 2.3 Preserve baseline and repeated `--change` CLI ordering in comparison CSV columns and support both single-run roots and repeated-run `summary/` roots; add CLI-level tests for argument and output handling.
- [x] 2.4 Document comparison CLI invocation, compatible-input requirements, output layout, column semantics, and failure behavior in `README.md`; verify examples use the implemented arguments.

## 3. Integration Validation

- [x] 3.1 Run `make test` and `make mypy` after both utilities and report publication changes; resolve all failures.
- [x] 3.2 Run `openspec validate complete-run-summary-and-comparison --strict` and verify the documented recovery and comparison workflows do not modify device state.
