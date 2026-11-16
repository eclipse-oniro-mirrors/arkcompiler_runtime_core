# Tasks

## 1. Preserve Primary Smaps Metrics

- [x] 1.1 Replace aggregate shared/private profile fields with clean, dirty, and per-mapping total metrics; update smaps parsing and profile addition so totals equal clean plus dirty, and verify focused `test.smaps_test` coverage.
- [x] 1.2 Update aggregate metric headers, statistics, plots, view parsing, comparison fixtures, and tests for the breaking metric schema; verify `make pylint` and affected report/view/comparison tests pass.

## 2. Generate Categorized Memdump Reports

- [x] 2.1 Add the `memdumps/` store directory, manifest, and per-pair paths; define the ordered first-match classifier for ArkTS dynamic/static, native, package, shared-object, JSVM, and fallback categories; verify focused classifier tests cover every configured rule and fallback.
- [x] 2.2 Aggregate each retained app/snapshot source into zero-filled fixed categories, derive per-source totals before statistics, and write ordered per-pair memdump CSVs plus their manifest with the shared aggregate schema; verify report tests cover filtering, zero-filled samples, category order, total-last placement, paths, and metric values.

## 3. Validate and Compare Standalone Views

- [x] 3.1 Require and parse declared `memdumps/<snapshot>/<app>.csv` reports in standalone views, retaining rows keyed by pair and validating the manifest, headers, category rows, numeric cells, matching sample counts, and linear mean-total invariant; verify malformed or missing manifest/report fixtures are rejected.
- [x] 3.2 Extend `compare.py` to write comparative `memdumps/<snapshot>/<app>.csv` reports with baseline-order categories, medians, and percentage deltas; verify zero baselines, multiple changes, and differing values retain existing Decimal and text behavior.
- [x] 3.3 Update view-layout, evidence, integration, and comparison tests for the required memdump tree, comparison output, and obsolete root CSV schema; verify `make tests_full` passes.

## 4. Final Verification

## 4. Add Footprint Reports

- [x] 4.1 Add root `footprint.csv` generation and view validation for source-level `footprint` and `private_clean_kb` statistics; verify report and malformed-view coverage.
- [x] 4.2 Extend `compare.py` to write comparative root `footprint.csv`; verify medians, multiple changes, and zero-baseline percentages.
- [x] 4.3 Update integration fixtures and run `make tests_full`.

## 5. Final Verification

- [x] 5.1 Run `make pylint`, `make tests_full`, `git diff --check`, and `openspec validate add-memdump-view-report --strict`; verify all commands succeed.
