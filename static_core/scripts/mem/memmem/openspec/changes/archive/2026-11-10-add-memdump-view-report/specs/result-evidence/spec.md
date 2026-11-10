# Spec Delta

## MODIFIED Requirements

### Requirement: New views record parameters and final ordered coverage
A view SHALL exclusively create its exact destination and contain root `metadata.json`, retained `summary.csv`, `outliers.csv`, eager `plots/`, eager `breakdowns/metadata.json` plus declared CSVs, and eager `memdumps/metadata.json` plus declared CSVs. Root metadata SHALL contain ordered nonempty duplicate-free normalized absolute source `dirs`, `smaps_filter` string and integer `smaps_filter_flags` equal to the compiled pattern's effective flags. No app metadata SHALL be embedded. Breakdown and memdump metadata SHALL use the artifact schema with final `<snapshot>/<app>.csv` paths, timestamps from the first retained source, and ordered event/file subsequences from reference evidence. Only pairs with retained contributions SHALL be declared; empty events SHALL be omitted. Empty views SHALL have empty artifact arrays, header-only `summary.csv`, empty breakdown and memdump manifests, and no plots, while outlier audit follows normal selection. No root `memdump.csv`, `summary_filtered.csv`, pretrim summary, raw-summary variant or log plot SHALL be written. New partial view output SHALL remain on failure; existing destinations SHALL be untouched. View creation SHALL not modify evidence or reopen generated per-source reports.

#### Scenario: One source is supplied
- **WHEN** a view is created from one valid evidence directory
- **THEN** it uses the same aggregate schema as multiple sources, emits declared `memdumps/<snapshot>/<app>.csv` reports, and emits a header-only discarded audit

#### Scenario: Filter excludes one app at an event
- **WHEN** retained sources contribute no matching mappings for one declared app/snapshot pair
- **THEN** that pair is absent from summary, memdump and breakdown files/manifests, and plots without being replaced by zeros

#### Scenario: All pairs are excluded
- **WHEN** no retained pair contributes data
- **THEN** required directories and metadata still exist, breakdown and memdump artifacts are empty, summary is header-only, and no plots are written

#### Scenario: Rendering fails
- **WHEN** output writing or plotting fails after the new destination was created
- **THEN** the operation raises and leaves the new partial view without staging cleanup, backup or restoration

### Requirement: View statistics preserve whole-source outlier policy
Views SHALL parse declared raw smaps with the requested compiled pattern and aggregate retained sources directly. Metric order SHALL be `size_kb`, `rss_kb`, `pss_kb`, `referenced_kb`, `shared_clean_kb`, `shared_dirty_kb`, `shared_total_kb`, `private_clean_kb`, `private_dirty_kb`, `private_total_kb`, `swap_kb`, `swap_pss_kb`, `anonymous_kb`; statistic order SHALL be `mean`, `geomean`, `median`, `std`, `min`, `max`. A mapping's shared and private total values SHALL equal its respective clean plus dirty values before cross-source aggregation. Summary header SHALL be `app_label,snapshot_label,n_samples` then metric-major/statistic-minor columns; breakdown header SHALL be `tag,n_samples` then the same columns. Pair sample counts SHALL count contributing retained sources; tag counts SHALL count sources containing that tag, without zero-filling absence. Std SHALL be sample deviation and zero for one value; geomean SHALL be zero if any contribution is zero. Counts/min/max SHALL be integers and other written statistics SHALL have two decimal digits. Tags SHALL sort by descending mean size then ascending tag.

Outlier discard count for N inputs SHALL be `max(0,min(floor(0.20*N),N-2,N-ceil(0.60*N)))`. Score SHALL be the median of absolute relative deviations of present cells from their cross-source cell medians; missing cells SHALL be ignored, zero-median cells SHALL contribute zero, and empty sources SHALL score zero while still counting in N. Select descending scores, breaking ties by ascending source order, and discard entire sources. Final pair order SHALL follow reference evidence's ordered snapshot/file declarations restricted to retained contributions, NOT first-seen contributing rows. `outliers.csv` SHALL have header `discarded` and normalized full source paths in input order. No summary of discarded-inclusive values SHALL be emitted.

#### Scenario: First source lacks an early filtered row
- **WHEN** a later retained source contributes an earlier declared snapshot missing from the first source's filtered data
- **THEN** summary and breakdown metadata still use declaration order rather than first-seen row order

#### Scenario: Empty sources participate in scoring
- **WHEN** valid evidence parses to no matching measurements
- **THEN** it remains in the population with score zero and can be discarded under the deterministic tie/quota policy

#### Scenario: A tag has fewer contributors than its pair
- **WHEN** a tag is absent in some retained sources but the pair has other matching tags
- **THEN** its sample count includes only sources containing that tag and no zero is inserted for absence

#### Scenario: Matching zero-valued mapping exists
- **WHEN** a mapping matches but all supported values are zero
- **THEN** it contributes a zero-valued measurement rather than an absent row

#### Scenario: Shared and private clean/dirty values are present
- **WHEN** a parsed mapping contains `Shared_Clean`, `Shared_Dirty`, `Private_Clean`, and `Private_Dirty` values
- **THEN** the summary and breakdown reports contain distinct clean, dirty, and total metrics for each memory kind

## ADDED Requirements

### Requirement: Footprint report aggregates derived source metrics
Every view SHALL write root-level `footprint.csv` with header `app_label,snapshot_label,n_samples` followed by metric-major/statistic-minor columns for `footprint` then `private_clean_kb`; statistic order SHALL be `mean`, `geomean`, `median`, `std`, `min`, `max`. Each pair row SHALL have the same identity and `n_samples` as its summary row. Each contributing source's `footprint` value SHALL equal its parsed `pss_kb + swap_pss_kb` before statistics are calculated; its `private_clean_kb` value SHALL be the parsed primary metric. Empty views SHALL write a header-only footprint report.

#### Scenario: Sources have distinct footprint values
- **WHEN** retained sources contribute different PSS, SwapPss, or private-clean values for a pair
- **THEN** footprint and private-clean statistics describe their per-source values rather than statistics derived from summary cells

### Requirement: Categorized memdump report aggregates filtered mappings
Every view SHALL write an eager `memdumps/` directory with `metadata.json` and one declared `<snapshot>/<app>.csv` report for every app/snapshot pair with retained contributions. The manifest SHALL use the same ordered artifact schema as breakdown metadata and declare the same pair identities. Each report SHALL have header `category,n_samples` followed by the same metric-major/statistic-minor columns as `summary.csv`, and SHALL write exactly these categories in order: `arkts-dyn-heap`, `arkts-sta-heap`, `native-heap`, `.hap`, `.so`, `jsvm-heap`, `other`, and `total`.

The report SHALL classify only mappings already admitted by the view's requested smaps filter. It SHALL assign each admitted mapping to exactly one non-total category: `arkts-dyn-heap` for tags beginning `[anon:ArkTS Heap`; `arkts-sta-heap` for tags beginning `[anon:ArkTs Static Object Space]`, `[anon:ArkTs Static Humongous Object Space]`, or `[anon:ArkTs Static Non Movable Space]`; `native-heap` for `[heap]`, tags beginning `[anon:native_heap:`, or `[anon:libc_malloc]`; `.hap` for tags beginning `/data/storage` or ending `.hap` or `.hsp`; `.so` for tags ending `.so` or `.so.1`; and `jsvm-heap` for tags beginning `[anon:JSVM`. All remaining tags SHALL be assigned to `other`.

For each retained source contributing to a pair, every non-total category SHALL contribute one profile, using an all-zero profile when the source has no mapping in that category. The `total` profile for that source SHALL be the sum of its seven non-total category profiles. Consequently, all eight rows for a pair SHALL have that pair's retained-source `n_samples`, and every total metric statistic SHALL describe the corresponding per-source category sum.

#### Scenario: Filtered mappings populate only matching categories
- **WHEN** the view filter retains an ArkTS dynamic heap mapping, a shared-library mapping, and an unmatched mapping
- **THEN** their values contribute respectively to `arkts-dyn-heap`, `.so`, and `other`, while all unrelated categories contribute zero for that source

#### Scenario: A source lacks a selected category
- **WHEN** a retained source contributes a pair but has no JSVM heap mapping
- **THEN** its `jsvm-heap` profile is zero-valued and the pair's `jsvm-heap` row retains the same sample count as its `total` row

#### Scenario: Total is the final category
- **WHEN** a pair has retained mappings
- **THEN** `total` is its final memdump row and each total profile equals the sum of that source's preceding category profiles

## MODIFIED Requirements

### Requirement: View readers validate declarations before equivalence
A view reader SHALL require plots/, breakdowns/, and memdumps/ as directories; root metadata; breakdown and memdump metadata; aggregate `summary.csv`; and audit CSVs. It SHALL derive ordered pair identities from artifact labels and declared file stems, require matching breakdown and memdump declarations, read only declared CSVs, and verify that summary identities exactly match that ordered sequence. It SHALL reject duplicate labels/files/pairs/tags, missing declared files, wrong headers/row widths, legacy root memdump schemas, non-finite/negative numeric values, nonpositive or noninteger sample counts, and noninteger min/max. Declared breakdowns SHALL contain at least one tag. Every declared memdump report SHALL contain exactly the ordered categories for its matching summary pair, retain its sample count, and reject a total whose mean does not equal the sum of its non-total category means for any metric. Audit paths SHALL be unique members of root source dirs. Extra undeclared files SHALL be ignored. The reader SHALL NOT validate plot contents/completeness, reopen source evidence or recompute statistics beyond the memdump mean-total invariant, and SHALL NOT require sums of breakdown tag means to equal summary means. View equivalence SHALL compare regex source, effective flags, ordered breakdown labels and ordered file arrays only; it SHALL ignore timestamps, values, source count/paths, sample counts, outliers, and memdump values. Matching labels across views SHALL be documented as caller assertions of semantic comparability.

#### Scenario: Source evidence has moved
- **WHEN** valid view files exist but source paths in metadata are unavailable
- **THEN** reading and comparing the view succeed without accessing those paths

#### Scenario: Summary order disagrees with metadata
- **WHEN** summary has the correct pair members in a different order
- **THEN** the reader rejects the view before equivalence or numerical comparison

#### Scenario: Regex flags differ
- **WHEN** two validated views have the same pattern text and declarations but different effective flags
- **THEN** they are not equivalent

#### Scenario: Non-median statistic is malformed
- **WHEN** a CSV median is valid but another required statistic is non-finite or malformed
- **THEN** the reader rejects the view rather than validating only comparison-selected columns

#### Scenario: Mandatory view directory is missing
- **WHEN** even an otherwise empty view lacks plots/ or breakdowns/, or has a file in place of either directory
- **THEN** the reader rejects its structural layout without attempting to verify individual plot files

#### Scenario: Memdump is missing or malformed
- **WHEN** a view lacks `memdumps/`, its manifest, or a declared report; has a root `memdump.csv`; omits a category; changes category order; or has a category total inconsistent with its children
- **THEN** the reader rejects the view
