# result-evidence Specification

## Purpose
Defines the benchmark output layout, raw evidence handling, artifact receive behavior, and generated CSV reports.

## Requirements

### Requirement: Standalone evidence declares expected artifact files
An execution SHALL exclusively create its exact evidence destination before device actions. It SHALL write `apps.json` with an `apps` array of PID, label, bundle, and ability records in launch order; eager `snapshots/metadata.json` and `screenshots/metadata.json`; raw snapshots at `snapshots/<snapshot_label>/<app_label>.smaps`; screenshots at `screenshots/<screenshot_label>.png`; and optional device logs at `logs/log.log`. It SHALL NOT write flow copies or reports inside evidence. Each artifact metadata file SHALL have an ordered `artifacts` array; each artifact SHALL contain a valid label, a nonnegative integer nanosecond timestamp serialized as a decimal string, and a required nonempty ordered `files` array of relative forward-slash paths. Empty artifact arrays SHALL be accepted. Metadata SHALL declare expected files before capture rather than only received files. Source app and artifact labels SHALL retain existing filename-safe validation. Different app labels resolving to the same PID SHALL remain distinct.

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
Evidence readers SHALL require `apps.json`, both eager artifact directories and their metadata files, valid unique app/category-event identities, required nonempty ordered file arrays, correct relative path structures/extensions, and all declared files as readable files. Snapshot paths SHALL be exactly `<artifact.label>/<known-app-label>.smaps`; screenshot paths SHALL be exactly `<artifact.label>.png`. Absolute/traversing paths and duplicate file identities SHALL be rejected. Undeclared extra files and all log contents/availability SHALL be ignored. Evidence equivalence SHALL compare app label-to-bundle/ability mappings ignoring PIDs and app-array ordering, then ordered snapshot labels and ordered file arrays ignoring timestamps. Screenshots and logs SHALL NOT affect equivalence. Invalid sources SHALL fail rather than be silently skipped. Legacy structures SHALL be rejected. Documentation and code contracts SHALL explicitly state that structural validity does not prove whole-flow completion or file-content integrity; those remain caller responsibility/out of scope.

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

### Requirement: New views record parameters and final ordered coverage
A view SHALL exclusively create its exact destination and contain root `metadata.json`, retained `summary.csv`, `outliers.csv`, eager `plots/`, and eager `breakdowns/metadata.json` plus declared CSVs. Root metadata SHALL contain ordered nonempty duplicate-free normalized absolute source `dirs`, `smaps_filter` string and integer `smaps_filter_flags` equal to the compiled pattern's effective flags. No app metadata SHALL be embedded. Breakdown metadata SHALL use the artifact schema with final `<snapshot>/<app>.csv` paths, timestamps from the first retained source, and ordered event/file subsequences from reference evidence. Only pairs with retained contributions SHALL be declared; empty events SHALL be omitted. Empty views SHALL have empty artifact arrays and header-only summary, while outlier audit follows normal selection. No `summary_filtered.csv`, pretrim summary, raw-summary variant or log plot SHALL be written. New partial view output SHALL remain on failure; existing destinations SHALL be untouched. View creation SHALL not modify evidence or reopen generated per-source reports.

#### Scenario: One source is supplied
- **WHEN** a view is created from one valid evidence directory
- **THEN** it uses the same aggregate schema as multiple sources and emits a header-only discarded audit

#### Scenario: Filter excludes one app at an event
- **WHEN** retained sources contribute no matching mappings for one declared app/snapshot pair
- **THEN** that pair is absent from summary, breakdown files, breakdown manifest and plots without being replaced by zeros

#### Scenario: All pairs are excluded
- **WHEN** no retained pair contributes data
- **THEN** required directories and metadata still exist, breakdown artifacts are empty, summary is header-only, and no plots are written

#### Scenario: Rendering fails
- **WHEN** output writing or plotting fails after the new destination was created
- **THEN** the operation raises and leaves the new partial view without staging cleanup, backup or restoration

### Requirement: View statistics preserve whole-source outlier policy
Views SHALL parse declared raw smaps with the requested compiled pattern and aggregate retained sources directly. Metric order SHALL be size_kb, rss_kb, pss_kb, referenced_kb, shared_kb, private_kb, swap_kb, swap_pss_kb, anonymous_kb; statistic order SHALL be mean, geomean, median, std, min, max. Summary header SHALL be `app_label,snapshot_label,n_samples` then metric-major/statistic-minor columns; breakdown header SHALL be `tag,n_samples` then the same columns. Pair sample counts SHALL count contributing retained sources; tag counts SHALL count sources containing that tag, without zero-filling absence. Std SHALL be sample deviation and zero for one value; geomean SHALL be zero if any contribution is zero. Counts/min/max SHALL be integers and other written statistics SHALL have two decimal places. Tags SHALL sort by descending mean size then ascending tag.

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

### Requirement: View readers validate declarations before equivalence
A view reader SHALL require plots/ and breakdowns/ as directories, root and breakdown metadata, and aggregate summary/audit CSVs, derive ordered pair identities from artifact labels and declared file stems, read only declared breakdown CSVs, and verify that summary identities exactly match that ordered sequence. It SHALL reject duplicate labels/files/pairs/tags, missing declared files, wrong headers/row widths, legacy schemas, non-finite/negative numeric values, nonpositive or noninteger sample counts, and noninteger min/max. Declared breakdowns SHALL contain at least one tag. Audit paths SHALL be unique members of root source dirs. Extra undeclared files SHALL be ignored. The reader SHALL NOT validate plot contents/completeness, reopen source evidence or recompute statistics, and SHALL NOT require sums of tag means to equal summary means. View equivalence SHALL compare regex source, effective flags, ordered breakdown labels and ordered file arrays only; it SHALL ignore timestamps, values, source count/paths, sample counts, and outliers. Matching labels across views SHALL be documented as caller assertions of semantic comparability.

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

### Requirement: All views use aggregate linear SVG plots
Every nonempty view SHALL generate per-app per-metric SVGs at `plots/<app>/<app>-<metric>.svg` from retained aggregate rows, plotting geomean with std error bars even for a single source. Timestamps SHALL come from the already loaded first retained source. Points SHALL sort by timestamp with stable ties; each app's origin SHALL be its first plotted timestamp. Missing timestamps for plotted labels SHALL be errors, not skipped points. Empty results SHALL be valid without plots. Rendering SHALL retain headless output, dotted lines, rotated unwrapped labels, adaptive spacing for dense labels, grids behind data, and dense y ticks. Log-transformed and cross-app plots SHALL NOT be generated.

#### Scenario: First supplied source is discarded
- **WHEN** outlier selection discards the first supplied source
- **THEN** plots and breakdown metadata use timestamps of the first retained source

#### Scenario: Early snapshot has no matching data
- **WHEN** filtering omits an app's earlier event
- **THEN** its first remaining plotted event defines time zero

#### Scenario: Plotted timestamp is missing
- **WHEN** a final row has no timestamp in the reference map
- **THEN** view generation raises and preserves partial output

### Requirement: No-aggregation warnings are scoped to view creation
For each declared smaps file whose parse returns no aggregated mappings, the system SHALL emit one warning per view-creation call containing the file path and pattern, formatted `smaps file {path} has no mappings aggregated for pattern {pattern}`. This SHALL apply to empty parsed input and no-match filtering, including default `.*`, without treating either as a structural missing-file error. The parser SHALL remain logging-free. Warnings SHALL obey the caller's logger configuration.

#### Scenario: Individual and combined views parse the same file
- **WHEN** separate view calls encounter the same no-aggregation file
- **THEN** each call can emit its own one-per-file warning

#### Scenario: Matching zero profile is parsed
- **WHEN** at least one mapping is aggregated with zero values
- **THEN** no no-aggregation warning is emitted for that file
