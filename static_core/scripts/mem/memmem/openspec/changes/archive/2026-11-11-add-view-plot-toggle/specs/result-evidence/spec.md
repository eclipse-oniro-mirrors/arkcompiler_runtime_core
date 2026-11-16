# Spec Delta

## MODIFIED Requirements

### Requirement: New views record parameters and final ordered coverage
A view SHALL exclusively create its exact destination and contain root `metadata.json`, retained `summary.csv`, `outliers.csv`, eager `plots/`, eager `breakdowns/metadata.json` plus declared CSVs, and eager `memdumps/metadata.json` plus declared CSVs. Root metadata SHALL contain ordered nonempty duplicate-free normalized absolute source `dirs`, `smaps_filter` string, integer `smaps_filter_flags` equal to the compiled pattern's effective flags, and required boolean `plots` recording whether plot rendering was enabled. No app metadata SHALL be embedded. Breakdown and memdump metadata SHALL use the artifact schema with final `<snapshot>/<app>.csv` paths, timestamps from the first retained source, and ordered event/file subsequences from reference evidence. Only pairs with retained contributions SHALL be declared; empty events SHALL be omitted. Empty views SHALL have empty artifact arrays, header-only `summary.csv`, empty breakdown and memdump manifests, and no plots, while outlier audit follows normal selection. No root `memdump.csv`, `summary_filtered.csv`, pretrim summary, raw-summary variant or log plot SHALL be written. New partial view output SHALL remain on failure; existing destinations SHALL be untouched. View creation SHALL not modify evidence or reopen generated per-source reports.

#### Scenario: Plot rendering is disabled
- **WHEN** a nonempty view is created with plots disabled
- **THEN** it contains an empty `plots/` directory, `metadata.json` has `plots` false, and all non-plot view reports are generated

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
- **WHEN** enabled plot rendering fails after the new destination was created
- **THEN** the operation raises and leaves the new partial view without staging cleanup, backup or restoration

### Requirement: All views use aggregate linear SVG plots
Every nonempty view with metadata `plots` true SHALL generate per-app per-metric SVGs at `plots/<app>/<app>-<metric>.svg` from retained aggregate rows, plotting geomean with std error bars even for a single source. Views with metadata `plots` false SHALL generate no SVG plots. Timestamps SHALL come from the already loaded first retained source. Points SHALL sort by timestamp with stable ties; each app's origin SHALL be its first plotted timestamp. Missing timestamps for plotted labels SHALL be errors, not skipped points. Empty results SHALL be valid without plots. Rendering SHALL retain headless output, dotted lines, rotated unwrapped labels, adaptive spacing for dense labels, grids behind data, and dense y ticks. Log-transformed and cross-app plots SHALL NOT be generated.

#### Scenario: Plot rendering is enabled by default
- **WHEN** a view is created without selecting a plot option
- **THEN** metadata has `plots` true and a nonempty view generates its SVG plots

#### Scenario: First supplied source is discarded
- **WHEN** outlier selection discards the first supplied source
- **THEN** plots and breakdown metadata use timestamps of the first retained source

#### Scenario: Early snapshot has no matching data
- **WHEN** filtering omits an app's earlier event
- **THEN** its first remaining plotted event defines time zero

#### Scenario: Plotted timestamp is missing
- **WHEN** plot rendering is enabled and a final row has no timestamp in the reference map
- **THEN** view generation raises and preserves partial output

### Requirement: View readers validate declarations before equivalence
A view reader SHALL require plots/, breakdowns/, and memdumps/ as directories; root metadata with required boolean `plots`; breakdown and memdump metadata; aggregate `summary.csv`; and audit CSVs. It SHALL derive ordered pair identities from artifact labels and declared file stems, require matching breakdown and memdump declarations, read only declared CSVs, and verify that summary identities exactly match that ordered sequence. It SHALL reject duplicate labels/files/pairs/tags, missing declared files, missing or nonboolean `plots`, wrong headers/row widths, legacy root memdump schemas, non-finite/negative numeric values, nonpositive or noninteger sample counts, and noninteger min/max. Declared breakdowns SHALL contain at least one tag. Every declared memdump report SHALL contain exactly the ordered categories for its matching summary pair, retain its sample count, and reject a total whose mean does not equal the sum of its non-total category means for any metric. Audit paths SHALL be unique members of root source dirs. Extra undeclared files SHALL be ignored. The reader SHALL NOT validate plot contents/completeness, reopen source evidence or recompute statistics beyond the memdump mean-total invariant, and SHALL NOT require sums of breakdown tag means to equal summary means. View equivalence SHALL compare regex source, effective flags, ordered breakdown labels and ordered file arrays only; it SHALL ignore metadata `plots`, timestamps, values, source count/paths, sample counts, outliers, and memdump values. Matching labels across views SHALL be documented as caller assertions of semantic comparability.

#### Scenario: View metadata omits plot policy
- **WHEN** a view metadata file lacks `plots`
- **THEN** the reader rejects the view

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
