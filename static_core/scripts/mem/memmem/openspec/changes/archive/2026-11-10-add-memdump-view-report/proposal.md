# Proposal

## Why

Views currently merge each mapping's clean and dirty shared/private memory before reporting it, obscuring useful memory-state detail. They also expose raw mapping breakdowns but no stable hidumper-style aggregation for the memory classes needed in benchmark analysis.

## What Changes

- **BREAKING** Replace ambiguous `shared_kb` and `private_kb` view metrics with clean, dirty, and total shared/private metrics.
- Keep `summary.csv` as the complete aggregate report of every primary metric.
- Add required `memdumps/` reports, aggregating filtered mappings into fixed hidumper-derived categories per app and snapshot.
- Emit zero-filled category samples so every emitted memdump category shares the pair's retained-source count; emit `total` last.
- Require standalone view readers to validate each declared memdump report and its category-total invariants.
- Add per-pair memdump reports to comparison output with the same median and percentage-delta presentation as existing aggregate comparison reports.
- Add root `footprint.csv`, reporting cross-source statistics for `footprint` (`pss_kb + swap_pss_kb`) and `private_clean_kb` per app/snapshot, plus comparative footprint output.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `result-evidence`: Revise view metrics, layout, and validation to add the categorized aggregate report.
- `result-comparison`: Revise comparison output to include the categorized aggregate report.

## Impact

- `src/smaps.py` retains clean and dirty smaps values as distinct primary metrics.
- `src/report.py`, `src/store.py`, and `src/view.py` write, validate, and expose `memdumps/<snapshot>/<app>.csv` reports.
- `compare.py` writes comparative `memdumps/<snapshot>/<app>.csv` reports.
- `src/report.py`, `src/store.py`, `src/view.py`, and `compare.py` write, validate, and compare `footprint.csv`.
- Existing views use an obsolete CSV schema and are not readable or comparable after the change.
- Parser, report, view, comparison, and integration tests require updated fixtures and assertions.
