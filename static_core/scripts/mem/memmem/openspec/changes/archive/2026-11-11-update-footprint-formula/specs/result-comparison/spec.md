# Spec Delta

## MODIFIED Requirements

### Requirement: Footprint comparison publishes derived aggregate changes
Comparison SHALL write root `footprint.csv` with `app_label,snapshot_label` followed by its existing metric-major baseline/change median and percentage-delta columns for `footprint`, `pss_plus_swap_pss`, and `private_clean_kb`, in that order. It SHALL preserve baseline pair order, original numeric text, Decimal percentage arithmetic, and blank percentages for zero baselines.

#### Scenario: Footprint baseline is zero
- **WHEN** a baseline footprint, PSS-plus-SwapPss, or private-clean median is zero
- **THEN** the comparison preserves numeric values and leaves that metric's percentage delta empty
