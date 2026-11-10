# Tasks

## 1. Derive and publish footprint metrics

- [x] 1.1 Extend the ordered `Footprint` source-value model so `footprint` is `pss_kb + 2 * swap_pss_kb`, `pss_plus_swap_pss` is `pss_kb + swap_pss_kb`, and `private_clean_kb` remains last; verify `test/profile_test.py` asserts the field order and nonzero-SwapPss values.
- [x] 1.2 Update footprint report generation and strict view-reader expectations to emit and require the three-metric CSV schema; verify `test/report_test.py` and `test/view_test.py` cover the header, values, and rejection of the former schema.

## 2. Compare and validate revised reports

- [x] 2.1 Update comparative footprint CSV generation to include `pss_plus_swap_pss` in shared metric order; verify `test/compare_test.py` asserts its baseline, change, and percentage columns.
- [x] 2.2 Run `make pylint`, `make mypy`, and `make test` to verify formatting, types, and the complete test suite for the changed report contract.
