# Spec Delta

## MODIFIED Requirements

### Requirement: Footprint report aggregates derived source metrics
Every view SHALL write root-level `footprint.csv` with header `app_label,snapshot_label,n_samples` followed by metric-major/statistic-minor columns for `footprint` then `private_clean_kb`; statistic order SHALL be `mean`, `geomean`, `median`, `std`, `min`, `max`. Each pair row SHALL have the same identity and `n_samples` as its summary row. Each contributing source's `footprint` value SHALL equal its parsed `pss_kb + swap_pss_kb` before statistics are calculated; its `private_clean_kb` value SHALL be the parsed primary metric. Empty views SHALL write a header-only footprint report. The metric order SHALL be derived from the footprint source-value model.

#### Scenario: Sources have distinct footprint values
- **WHEN** retained sources contribute different PSS, SwapPss, or private-clean values for a pair
- **THEN** footprint and private-clean statistics describe their per-source values rather than statistics derived from summary cells

## ADDED Requirements

### Requirement: Memdump categories have one ordered classification source
Memdump category names, their tag-matching patterns, and the order used in the memdump CSV SHALL derive from one ordered category-to-pattern source. Classification SHALL return the first matching category in that order and SHALL return `other` when no pattern matches. The category order and matching behavior for existing tags SHALL remain unchanged.

#### Scenario: A tag matches more than one category
- **WHEN** a tag matches patterns for multiple categories
- **THEN** it is assigned to the first matching category in the existing category order

#### Scenario: A tag matches no category pattern
- **WHEN** a tag has no configured category-pattern match
- **THEN** it is assigned to `other`
