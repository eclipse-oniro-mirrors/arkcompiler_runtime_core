# Proposal

## Why

The existing `footprint` metric represents hidumper's `PssTotal`, while the
external framework consumes hidumper's `PssTotal+SwapPss` footprint. The view
and comparison reports need to publish that framework-compatible value without
losing the current PSS-plus-SwapPss measurement.

## What Changes

- **BREAKING** Redefine each source's `footprint` as `pss_kb + 2 * swap_pss_kb`,
  matching hidumper `PssTotal+SwapPss` when hidumper `PssTotal` includes smaps
  `SwapPss`.
- Add the derived `pss_plus_swap_pss` metric with value
  `pss_kb + swap_pss_kb`.
- Retain `private_clean_kb` and use the ordered metric sequence `footprint`,
  `pss_plus_swap_pss`, `private_clean_kb` in view and comparison footprint
  CSVs.
- Update footprint CSV validation and tests for the changed schema and values.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `result-evidence`: Change the derived footprint view schema and calculation.
- `result-comparison`: Change comparative footprint output to include the new
  metric and revised footprint semantics.

## Impact

- `src/profile.py` changes the derived `Footprint` model and calculation.
- `src/report.py`, `src/view.py`, and `compare.py` inherit the ordered metric
  schema through `FOOTPRINT_METRICS`.
- Footprint report, view-reader, and comparison tests need updated expected
  values and headers.
