# Design

## Context

See [proposal.md](proposal.md) for motivation and [specs/result-evidence/spec.md](specs/result-evidence/spec.md) for the behavior contract. `MemProfile` currently collapses clean and dirty shared/private smaps fields, while view creation produces a general summary and per-tag breakdowns but no categorized root report. A standalone view reader strictly validates the generated summary and breakdown layout.

## Goals / Non-Goals

**Goals:**

- Preserve each clean and dirty smaps value and calculate its total at mapping granularity.
- Generate deterministic, hidumper-inspired categorized aggregates from the mappings admitted by the configured smaps filter.
- Make declared per-pair memdump reports self-validating required view artifacts.
- Compare validated memdump rows alongside summaries and tag breakdowns.
- Report and compare footprint and private-clean aggregate statistics in a concise root CSV.

**Non-Goals:**

- Reproduce every hidumper memory category, graphics collector value, or hidumper formatting detail.
- Change the outlier-selection policy or the existing tag breakdown behavior.
- Keep pre-change views readable or comparable.

## Decisions

### Replace ambiguous shared/private metrics

The primary profile will replace `shared_kb` and `private_kb` with clean, dirty, and total metrics for each. Totals are calculated when one smaps mapping is parsed, not from aggregate CSV cells. This preserves a complete source-level population for all statistics.

Alternative: retain only clean and dirty fields and derive totals in CSV output. Rejected because the total would lack independent source-level statistics and could not be validated consistently.

### Reuse the aggregate CSV metric schema

Each `memdumps/<snapshot>/<app>.csv` will use `category,n_samples` followed by the same metric/statistic columns as `summary.csv`. The `memdumps/metadata.json` manifest declares the ordered pair/file population using the same artifact structure as breakdowns. Each report aggregates only retained evidence and uses the same sample statistics.

Alternative: write one raw value per category. Rejected because a view can contain multiple retained sources and would require an arbitrary representative sample or statistic.

### Materialize a fixed category population per source

For every retained source that contributes an app/snapshot pair, the report creates profiles for all seven non-total categories. Categories without a matching mapping receive a zero profile. `total` is calculated by summing the seven profiles for that same source; aggregation happens only afterward.

This makes the report rectangular and makes every category's `n_samples` equal to the pair's count. It also makes total statistics meaningful at every statistic type, including min/max and geomean.

Alternative: aggregate only categories present in a source. Rejected because category sample counts would diverge and missing data could no longer be distinguished from zero-valued category presence.

### Use an ordered first-match classifier for filtered tags

The classifier operates on normalized breakdown tags after `parse_smaps_text` applies the user filter. It emits, in fixed order, `arkts-dyn-heap`, `arkts-sta-heap`, `native-heap`, `.hap`, `.so`, `jsvm-heap`, `other`, then `total`.

The recognized prefix/suffix forms are the relevant hidumper source conventions agreed during exploration. The `.hap` category deliberately includes `.hsp` and `/data/storage` mappings despite a current hidumper source/documentation inconsistency, so package mappings remain visible in the requested report.

Alternative: duplicate hidumper's full evolving classifier. Rejected because this feature intentionally reports a limited stable category set and does not consume hidumper output.

### Treat memdump as required view content and comparison input

The view store gets a dedicated memdumps directory, manifest, and per-pair path. View construction writes the manifest and declared reports with the other view artifacts. View reading validates the manifest, each report's category order, numeric cells, sample counts, and that every total mean equals the sum of its category means. It does not try to sum non-linear statistics such as geomean, median, or standard deviation.

The parsed view retains validated memdump rows keyed by pair. Once declarations are equivalent, each view has the same ordered summary pairs and the fixed ordered category set, so comparison can safely align each report positionally. Comparison writes `memdumps/<snapshot>/<app>.csv` with `category` followed by the existing metric-major base/change median and percentage-delta columns. It preserves baseline pair and category order, original numeric text, Decimal arithmetic, and blank percentages for zero baselines. The comparison report is not a view and has no metadata or independent validation contract.

Alternative: leave memdump as an optional informational file. Rejected because malformed or stale aggregate data would then be indistinguishable from valid view output.

### Emit footprint as a derived aggregate report

`footprint.csv` is a required root view artifact with `app_label,snapshot_label,n_samples` followed by metric-major statistics for `footprint` and `private_clean_kb`. For each contributing source, footprint is calculated as `pss_kb + swap_pss_kb` before statistics are aggregated. This retains mean, geomean, median, standard deviation, minimum, and maximum without deriving nonlinear values from already-aggregated cells.

The standalone reader validates footprint identities, sample counts, and numeric values against `summary.csv`. Comparison writes a root `footprint.csv` using its established base/change median and percentage-delta columns.

## Risks / Trade-offs

- [Existing views have obsolete headers] -> Intentionally reject them; regenerate views from evidence before comparison.
- [Hidumper evolves its category rules] -> Keep the selected mapping rules explicit and covered by fixture tests rather than claiming complete hidumper parity.
- [A category total differs from the all-mapping summary due to classification] -> Classifier is an exhaustive partition: every filtered tag maps to exactly one non-total category, and reader validation checks linear mean totals.
- [Expanded metric schema increases fixture churn] -> Use named expected profiles and shared header helpers in tests where practical.

## Migration Plan

1. Extend smaps profiles and update all aggregate headers, report generation, parsing, and validation together.
2. Add categorized aggregation and required memdump output to new views.
3. Update all checked fixtures and validate generated views plus summary, memdump, and breakdown comparison behavior.
4. Regenerate any external views from their evidence; rollback requires reverting the schema and removing the required memdump artifact, and cannot make newly written views compatible with the prior reader.
