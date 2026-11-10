# Spec Delta

## MODIFIED Requirements

### Requirement: Footprint report aggregates derived source metrics
Every view SHALL write root-level `footprint.csv` with header `app_label,snapshot_label,n_samples` followed by metric-major/statistic-minor columns for `footprint`, `pss_plus_swap_pss`, then `private_clean_kb`; statistic order SHALL be `mean`, `geomean`, `median`, `std`, `min`, `max`. Each pair row SHALL have the same identity and `n_samples` as its summary row. Each contributing source's `footprint` value SHALL equal its parsed `pss_kb + 2 * swap_pss_kb` before statistics are calculated. Each contributing source's `pss_plus_swap_pss` value SHALL equal its parsed `pss_kb + swap_pss_kb`, and its `private_clean_kb` value SHALL be the parsed primary metric. Empty views SHALL write a header-only footprint report. The metric order SHALL be derived from the footprint source-value model.

#### Scenario: Sources have distinct footprint values
- **WHEN** retained sources contribute different PSS, SwapPss, or private-clean values for a pair
- **THEN** footprint, PSS-plus-SwapPss, and private-clean statistics describe their per-source values rather than statistics derived from summary cells

#### Scenario: Source includes SwapPss
- **WHEN** a retained source has parsed `pss_kb` of 100 and `swap_pss_kb` of 20
- **THEN** its footprint source value is 140 and its PSS-plus-SwapPss source value is 120 before statistics are calculated
