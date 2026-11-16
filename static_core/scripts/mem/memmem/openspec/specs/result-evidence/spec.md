# result-evidence Specification

## Purpose
Defines the benchmark output layout, raw evidence handling, artifact receive behavior, and generated CSV reports.

## Requirements

### Requirement: Standalone evidence declares expected artifact files
An execution SHALL exclusively create its exact evidence destination before device actions. It SHALL write root `flow.json` containing the canonical validated flow being executed, root `metadata.json` containing exactly `flow` equal to `"flow.json"`, strict boolean `reboot`, and strict boolean `device_log`, and `apps.json` with an `apps` array of PID, label, bundle, and ability records in launch order; eager `snapshots/metadata.json` and `screenshots/metadata.json`; raw snapshots at `snapshots/<snapshot_label>/<app_label>.smaps`; screenshots at `screenshots/<screenshot_label>.png`; and optional device logs at `logs/log.log`. Canonical flow serialization SHALL retain description metadata and expanded macros. Provenance SHALL be written during local initialization before device preparation, including for direct library calls, and SHALL record configuration rather than completion. It SHALL NOT record hooks, repeats, or backend and SHALL NOT write reports inside evidence. Each artifact metadata file SHALL have an ordered `artifacts` array; each artifact SHALL contain a valid label, a nonnegative integer nanosecond timestamp serialized as a decimal string, and a required nonempty ordered `files` array of relative forward-slash paths. Empty artifact arrays SHALL be accepted. Metadata SHALL declare expected files before capture rather than only received files. Source app and artifact labels SHALL retain existing filename-safe validation. Different app labels resolving to the same PID SHALL remain distinct.

#### Scenario: Flow has no snapshot or screenshot commands
- **WHEN** execution succeeds without either artifact type
- **THEN** both category directories and metadata files exist with empty artifact arrays

#### Scenario: Snapshot captures multiple eligible apps
- **WHEN** a snapshot event starts with eligible labels App and Other
- **THEN** its ordered expected files are `<label>/App.smaps` and `<label>/Other.smaps` before capture, and one event timestamp applies to both

#### Scenario: Screenshot capture fails
- **WHEN** capture of a declared screenshot fails
- **THEN** execution fails and its expected PNG declaration remains in partial evidence rather than being removed

#### Scenario: Logs are disabled
- **WHEN** execution uses device_log false
- **THEN** no device-log setup, stream or receive is performed and no log directory is required

#### Scenario: Direct library execution initializes evidence
- **WHEN** local evidence initialization succeeds for a direct library call
- **THEN** root flow and metadata contain the canonical flow and actual reboot/device_log settings without CLI-only information

#### Scenario: Execution fails after local initialization
- **WHEN** device preparation or collection fails after provenance was written
- **THEN** the provenance remains with partial evidence and does not assert successful completion

### Requirement: Evidence finalization preserves acquired resources and errors
Execution SHALL finalize only resources acquired by that invocation. Successful captures SHALL be queued immediately, independently of expected declarations. Finalization SHALL stop started streams, write available metadata including empty categories after successful local initialization, attempt every queued receive, and clean an acquired remote root. A later capture or transfer failure SHALL NOT prevent receiving a successful prefix. Primary failures SHALL be preserved with secondary failure context. Existing local destinations SHALL never be modified; newly created local partial evidence SHALL remain on failure. Remote roots SHALL remain independently host-timestamped under `/data/local/tmp`, with snapshot/screenshot roots and an enabled logs root. Successful execution SHALL pass local structural validation before returning; cleanup failures SHALL still fail execution. Structural validation SHALL not perform report analysis.

#### Scenario: Destination already exists
- **WHEN** exclusive evidence creation fails because dst exists
- **THEN** no device action, metadata write, artifact receive or remote cleanup for that execution changes the existing destination

#### Scenario: A later app capture fails
- **WHEN** one file was captured successfully before a subsequent file fails
- **THEN** finalization still attempts to receive the successful file and leaves the expected failed file declared

#### Scenario: Receiving one artifact fails
- **WHEN** a queued receive fails
- **THEN** all other queued receives are attempted and execution raises while preserving partial local evidence

#### Scenario: Pre-flow remote setup fails
- **WHEN** a remote root was created but a subdirectory fails
- **THEN** the acquired remote root is cleaned and unacquired lifecycle phases are not finalized

#### Scenario: Collection returns success
- **WHEN** execution returns normally
- **THEN** all required local structure and declared snapshot/screenshot files pass the same structural checks used by view loading, without statistics or plots being generated

### Requirement: Evidence validity and equivalence are separate contracts
Evidence readers SHALL require root `metadata.json` with exactly `flow: "flow.json"`, strict boolean `reboot`, and strict boolean `device_log`, and readable root `flow.json` that validates as canonical flow. They SHALL retain both parsed provenance objects in parsed evidence. They SHALL require `apps.json`, both eager artifact directories and their metadata files, valid unique app/category-event identities, required nonempty ordered file arrays, correct relative path structures/extensions, and all declared files as readable files. Snapshot paths SHALL be exactly `<artifact.label>/<known-app-label>.smaps`; screenshot paths SHALL be exactly `<artifact.label>.png`. Absolute/traversing paths and duplicate file identities SHALL be rejected. Undeclared extra files and all log contents/availability SHALL be ignored. Evidence equivalence SHALL compare app label-to-bundle/ability mappings ignoring PIDs and app-array ordering, then ordered snapshot labels and ordered file arrays ignoring timestamps. Canonical flow, execution metadata, screenshots, and logs SHALL NOT affect equivalence. Invalid sources SHALL fail rather than be silently skipped. Legacy structures, including evidence missing the new provenance files, SHALL be rejected. Documentation and code contracts SHALL explicitly state that structural validity does not prove whole-flow completion or file-content integrity; those remain caller responsibility/out of scope. Readers SHALL NOT add flow-to-artifact execution-completeness verification.

#### Scenario: Declared snapshot is absent
- **WHEN** snapshot metadata declares a file that is missing
- **THEN** the reader fails with the affected path rather than treating it as an absent measurement

#### Scenario: Extra files are present
- **WHEN** otherwise valid evidence contains undeclared files
- **THEN** analysis ignores them and uses only declared files

#### Scenario: Metadata differs only in PID and timestamps
- **WHEN** app identities and ordered snapshot declarations match but PID or timestamps differ
- **THEN** the sources are equivalent

#### Scenario: Ordered file arrays differ
- **WHEN** two sources declare the same file members in a different order
- **THEN** they are not equivalent

#### Scenario: Non-memory evidence differs
- **WHEN** two individually valid sources have equivalent memory declarations but different screenshots or log settings
- **THEN** they remain equivalent

#### Scenario: Required provenance is invalid
- **WHEN** root metadata or flow is missing, malformed, has invalid field types, or metadata refers to another flow filename
- **THEN** reading evidence fails before analysis

#### Scenario: Provenance differs
- **WHEN** individually valid sources have equivalent memory declarations but different canonical flows or execution options
- **THEN** their parsed provenance remains accessible and they remain equivalent

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

### Requirement: No-aggregation warnings are scoped to view creation
For each declared smaps file whose parse returns no aggregated mappings, the system SHALL emit one warning per view-creation call containing the file path and pattern, formatted `smaps file {path} has no mappings aggregated for pattern {pattern}`. This SHALL apply to empty parsed input and no-match filtering, including default `.*`, without treating either as a structural missing-file error. The parser SHALL remain logging-free. Warnings SHALL obey the caller's logger configuration.

#### Scenario: Individual and combined views parse the same file
- **WHEN** separate view calls encounter the same no-aggregation file
- **THEN** each call can emit its own one-per-file warning

#### Scenario: Matching zero profile is parsed
- **WHEN** at least one mapping is aggregated with zero values
- **THEN** no no-aggregation warning is emitted for that file

### Requirement: Footprint report aggregates derived source metrics
Every view SHALL write root-level `footprint.csv` with header `app_label,snapshot_label,n_samples` followed by metric-major/statistic-minor columns for `footprint`, `pss_plus_swap_pss`, then `private_clean_kb`; statistic order SHALL be `mean`, `geomean`, `median`, `std`, `min`, `max`. Each pair row SHALL have the same identity and `n_samples` as its summary row. Each contributing source's `footprint` value SHALL equal its parsed `pss_kb + 2 * swap_pss_kb` before statistics are calculated. Each contributing source's `pss_plus_swap_pss` value SHALL equal its parsed `pss_kb + swap_pss_kb`, and its `private_clean_kb` value SHALL be the parsed primary metric. Empty views SHALL write a header-only footprint report. The metric order SHALL be derived from the footprint source-value model.

#### Scenario: Sources have distinct footprint values
- **WHEN** retained sources contribute different PSS, SwapPss, or private-clean values for a pair
- **THEN** footprint, PSS-plus-SwapPss, and private-clean statistics describe their per-source values rather than statistics derived from summary cells

#### Scenario: Source includes SwapPss
- **WHEN** a retained source has parsed `pss_kb` of 100 and `swap_pss_kb` of 20
- **THEN** its footprint source value is 140 and its PSS-plus-SwapPss source value is 120 before statistics are calculated

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

### Requirement: Memdump categories have one ordered classification source
Memdump category names, their tag-matching patterns, and the order used in the memdump CSV SHALL derive from one ordered category-to-pattern source. Classification SHALL return the first matching category in that order and SHALL return `other` when no pattern matches. The category order and matching behavior for existing tags SHALL remain unchanged.

#### Scenario: A tag matches more than one category
- **WHEN** a tag matches patterns for multiple categories
- **THEN** it is assigned to the first matching category in the existing category order

#### Scenario: A tag matches no category pattern
- **WHEN** a tag has no configured category-pattern match
- **THEN** it is assigned to `other`

### Requirement: Metadata models reject coercible Python values
Evidence and view metadata validation SHALL reject incompatible builtin Python values rather than coercing them. It SHALL continue to accept valid serialized JSON evidence and view metadata, including serialized path values used by the existing metadata format.

#### Scenario: Direct metadata construction receives a coercible value
- **WHEN** evidence or view metadata construction receives a numeric string for an integer field, an integer for a boolean field, or another incompatible builtin value
- **THEN** validation fails without producing or modifying metadata

#### Scenario: Valid metadata JSON is read
- **WHEN** a reader loads evidence or view metadata JSON that satisfies the existing format and field types
- **THEN** it accepts the metadata and preserves the existing reader behavior
