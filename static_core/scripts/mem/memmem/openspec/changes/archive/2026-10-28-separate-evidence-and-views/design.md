# Design

## Context

See `proposal.md` for motivation. The current facade (`lib.py:153-188`) combines repeated execution, report generation, and averaging. `ResultStore` mixes device evidence with report paths. `generate_reports` reconstructs app/snapshot pairs and skips missing files; `average_reports` reads generated CSVs, scans breakdown directories, and owns a `summary/` destination. Comparison infers aggregate schemas from `summary_filtered.csv`. These boundaries must be replaced, not hidden beneath renamed entrypoints.

Current mathematical helpers, named app/snapshot identity, device protocol, flow builders/preprocessing, process finalization, and plotting presentation can be retained. The two independent static reviews identified ordering, CSV validation, destination ownership, failed-capture bookkeeping, and specification drift as the principal migration risks.

## Goals / Non-Goals

**Goals:** exact-destination public APIs; structurally validated independently analyzable evidence; multiple independent views; one aggregate schema for any nonzero source count; metadata-defined identity/order; standalone view comparison; minimal reusable models; fail-fast errors with debug artifacts preserved.

**Non-Goals:** whole-flow completion proof, checksums/content-integrity validation, auto-recovery/resume, legacy readers/migration adapters, symlink resolution, source/destination ancestry checks, atomic publication/staging/backups, removal of failed local output, new dependencies, altered UI/backend transport semantics, changed outlier algorithms, log plots, run-root metadata. Parent `flow.json` is provenance, not a prerequisite for analyzing copied evidence. Document that structurally valid evidence does not prove all intended flow commands ran.

## Decisions

### 1. Public API layout and signatures

Use standard-library `pathlib.Path` and `re.Pattern[str]`. Public interfaces in `lib.py`:

```python
MATCH_ALL: re.Pattern[str] = re.compile(".*")

def execute_flow(
    flow: Flow,
    device: Device,
    dst: pathlib.Path,
    *,
    reboot: bool = False,
    device_log: bool = True,
) -> pathlib.Path: ...

def create_view(
    dirs: list[pathlib.Path],
    dst: pathlib.Path,
    smaps_filter: re.Pattern[str] = MATCH_ALL,
) -> pathlib.Path: ...
```

- Remove `lib.run` and its export, not a deprecation wrapper. Preserve all flow/command builders, preprocessing, device constructors, public type exports, and logger helpers with their existing signatures.
- `execute_flow` accepts canonical `Flow`, revalidates it and checks device command support before evidence destination reservation or device actions. It performs one execution only: no repeats, regex, view generation, parent flow persistence, or logger setup/reset.
- `create_view` accepts a nonempty ordered list of exact evidence roots. Reject empty lists, duplicate lexically normalized absolute inputs, `None` filters, bytes patterns, and non-pattern values before analysis/output writes. The default is a compiled string pattern, never `None`.
- Each API creates its exact `dst` with `exist_ok=False`, returns its normalized absolute `Path`, and raises on failure. No API invents a timestamp or appends `evidence/` or `view/` to `dst`. Parent directories may exist and can be created as needed. Existing exact destinations are never modified. Newly created partial output is retained on failure.
- `execute_flow` success means local evidence passes shared structural validation. This does not promise that later rendering to an arbitrary destination or combining incompatible sources succeeds. Successful cleanup remains part of the existing execution contract; cleanup-only failure still raises even if evidence happens to be usable.

### 2. Storage and parsed models: minimum entities

Keep storage classes in `src/result.py`; introduce focused local reading in `src/evidence.py` and `src/view.py`. These are modules, not new public facade APIs.

```python
class EvidenceStore:
    def __init__(self, local_root: Path,
                 remote_root: PurePosixPath | None = None) -> None: ...
    def local_out_dir(self) -> Path: ...
    def local_app_metadata_path(self) -> Path: ...   # apps.json
    def local_snapshots_dir(self) -> Path: ...
    def local_screenshots_dir(self) -> Path: ...
    def local_artifact_metadata_path(self, category: Path) -> Path: ...
    def local_snapshot_path(self, snapshot_label: str, app_label: str) -> Path: ...
    def local_screenshot_path(self, screenshot_label: str) -> Path: ...
    def local_logs_dir(self) -> Path: ...
    def local_log_path(self) -> Path: ...
    def snapshot_relative_parts(self, snapshot_label: str, app_label: str) -> list[str]: ...
    def screenshot_relative_parts(self, screenshot_label: str) -> list[str]: ...
    def log_relative_parts(self) -> list[str]: ...
    # Remote counterparts return PurePosixPath and require remote_root.
    def remote_out_dir(self) -> PurePosixPath: ...
    def remote_snapshots_dir(self) -> PurePosixPath: ...
    def remote_snapshot_dir(self, snapshot_label: str) -> PurePosixPath: ...
    def remote_snapshot_path(self, snapshot_label: str, app_label: str) -> PurePosixPath: ...
    def remote_screenshots_dir(self) -> PurePosixPath: ...
    def remote_screenshot_path(self, screenshot_label: str) -> PurePosixPath: ...
    def remote_logs_dir(self) -> PurePosixPath: ...
    def remote_log_path(self) -> PurePosixPath: ...

class ViewStore:
    def __init__(self, root: Path) -> None: ...
    def local_out_dir(self) -> Path: ...
    def local_metadata_path(self) -> Path: ...
    def local_summary_path(self) -> Path: ...
    def local_outliers_path(self) -> Path: ...
    def local_breakdowns_dir(self) -> Path: ...
    def local_breakdown_metadata_path(self) -> Path: ...
    def local_breakdown_path(self, snapshot_label: str, app_label: str) -> Path: ...
    def local_plots_dir(self) -> Path: ...
    def local_plot_path(self, app_label: str, metric: str) -> Path: ...
```

Stores only calculate paths; they do not create directories or launch operations. Remote access on `EvidenceStore(..., remote_root=None)` raises a clear error, rather than using a dummy `/` or empty remote root. `ViewStore` has no remote methods. Remove the old mixed `ResultStore`, raw-report path methods on evidence storage, and evidence `local_flow_path`.

Reuse the existing `AppMetadataFile`, `ArtifactMetadataFile`, `MemProfile`, `SmapsSummary`, `SummaryRow`, `AveragedRow`, and named `AppSnapshotLabelPair` where useful. Move the existing pair type to a shared lower-level module (e.g. `src/metadata.py`) if needed to avoid circular imports; do not invent a parallel pair identity.

- `ParsedEvidence`: `source: Path`, `apps: AppMetadataFile`, `snapshots: ArtifactMetadataFile`, and `measurements: dict[AppSnapshotLabelPair, SmapsSummary]`. Measurements are filtered for this parse call; omitted pairs have no matching mappings. Screenshots are structurally checked but need not be retained. No `Evidence` or `EvidenceShape` objects.
- `ParsedView`: root/store reference, typed root view metadata, breakdown `ArtifactMetadataFile`, validated ordered summary records, and validated per-pair tag records. Reuse the comparison `Value`/Decimal representation as appropriate to preserve original CSV text and exact percentage computation; do not force view-reader floats into existing Decimal arithmetic. No separate view-shape model.

Internal boundary signatures:

```python
def validate_evidence(store: EvidenceStore) -> tuple[AppMetadataFile, ArtifactMetadataFile]: ...
def parse_evidence(store: EvidenceStore, smaps_filter: re.Pattern[str]) -> ParsedEvidence: ...
def is_evidence_equivalent(l: ParsedEvidence, r: ParsedEvidence) -> bool: ...
def read_view(store: ViewStore) -> ParsedView: ...
def is_view_equivalent(l: ParsedView, r: ParsedView) -> bool: ...
def parse_smaps_text(text: str, tag_filter: re.Pattern[str] = MATCH_ALL) -> SmapsSummary | None: ...
def normalize(path: Path) -> Path: ...
def absolutize(path: Path) -> Path: ...
```

`validate_evidence` loads schemas and checks declared paths/coverage, including screenshots, and returns the existing app/snapshot models. It performs no regex filtering, metric parsing, statistics, plotting, or device work. `parse_evidence` reuses it, then parses each declared smaps file once. There is no extra decision pending about this separation. Equivalence after parsing is acceptable; avoid adding a shape class just to optimize validation order.

### 3. Filesystem contracts

Successful combined CLI layout:

```text
<out-dir>/
  flow.json
  iteration_0/
    evidence/
      apps.json
      snapshots/
        metadata.json
        <snapshot_label>/<app_label>.smaps
      screenshots/
        metadata.json
        <screenshot_label>.png
      logs/log.log                    # only if device_log enabled
    view/view-<timestamp>/...
  iteration_1/...
  view/view-<timestamp>/...
```

Even one repeat uses `iteration_0` and two views (individual and combined). `execute_flow.py` produces only the flow and iteration evidence portions. No run-root `metadata.json`, per-iteration flow copy, report CSVs inside evidence, or `summary/` recovery directory.

Concrete view layout:

```text
<dst>/
  metadata.json
  summary.csv
  outliers.csv
  breakdowns/
    metadata.json
    <snapshot_label>/<app_label>.csv
  plots/<app_label>/<app_label>-<metric>.svg
```

`plots/` and `breakdowns/` always exist. Do not generate `summary_filtered.csv`, all-samples summary output, raw single-run summary variants, log plots, or cross-app plots. Comparison results use comparative columns and are not views; do not add view metadata merely to share writers.

### 4. Ordered declarations and strict local readers

Artifact metadata uses the existing file wrapper:

```json
{"artifacts":[{"label":"start","timestamp":"1760000000000000000","files":["start/App.smaps","start/Other.smaps"]}]}
```

`files` is required, nonempty, ordered, and serialized as relative forward-slash strings. Internally use relative `PurePosixPath` values if useful; never host-absolutize them. `artifacts` may be empty. Timestamp remains a decimal-string nonnegative integer in nanoseconds. Labels obey the existing filename-safe label rule. Require unique app labels, event labels within category, and declared file identities; reject absolute paths, traversal, wrong extensions, incorrect parent/event labels, or unknown snapshot app stems. Snapshots have exactly `<event.label>/<app>.smaps`; screenshots exactly `<event.label>.png` and one file per event. Empty `files` is invalid, not a way to represent failed receiving.

Both evidence artifact directories and metadata files are eager and mandatory, even with no commands of that type. All declared snapshot/screenshot files must exist as files. Ignore undeclared extras and logs entirely during validation. Do not infer measurements from directory scans. A structural reader cannot prove full-flow completion or complete smaps contents; record this explicitly in code/spec, and retain existing tolerant metric-line parsing.

Root view metadata:

```json
{"dirs":["/absolute/run/iteration_0/evidence"],"smaps_filter":".*\\.so","smaps_filter_flags":32}
```

Store `pattern.pattern` and `int(pattern.flags)`, including effective flags; no renamed `smaps_flags` field. `dirs` preserves input order and is nonempty, normalized, absolute, and duplicate-free. View metadata is provenance; the reader must not open these paths. Validate the stored pattern/flags as a valid string regex configuration.

Breakdown metadata has the same ordered artifact schema, with `<snapshot>/<app>.csv` files. It describes FINAL retained output only. Omit noncontributing files and then events with no files. Copy each event timestamp from the first retained source. Never blindly translate all source `.smaps` declarations into `.csv` declarations. Empty view: `{"artifacts":[]}`.

The view reader requires `plots/` and `breakdowns/` to be directories, including in empty views; it does not validate plot file contents or completeness. It derives `(app, snapshot)` from each ordered file stem and its artifact label, validates the path relationship, and checks that `summary.csv` contains exactly that ordered identity sequence. Read only declared breakdown CSVs. Require at least one tag row in each declared breakdown, unique tags, exact header and row width, positive integer `n_samples`, nonnegative integer min/max, and finite nonnegative mean/geomean/median/std. Writer emits four floating statistics with two decimals; readers validate numeric meaning without recomputing statistics. Validate every metric/statistic, not just medians. Reject duplicate identities before dictionary conversion. Reject legacy schemas and filenames without fallback. Do not require tag-mean sums to equal summary means (different sample populations).

`outliers.csv` has one column `discarded`, normalized absolute source paths in original source order. No duplicates; each entry must occur in view metadata `dirs`. No discards means header-only, including every single-source view. An all-empty multi-source view can still discard sources under the existing quota policy.

### 5. Equivalence without extra models

`is_evidence_equivalent` compares app label -> (bundle, ability) mappings, ignoring PID and app-array order, then compares ordered `(artifact.label, artifact.files)` snapshot sequences. File arrays are ordered, NOT sets. Ignore source paths, timestamps, contents, screenshots, and logs. Local validity is established separately before equivalence.

`is_view_equivalent` compares exact regex source and effective flags plus ordered `(label, files)` breakdown sequences. It ignores timestamps, values, tags, source paths/counts, sample counts, and outlier choices. Use one private ordered-artifact comparison function for both predicates. Do not use whole-model equality or sort declarations. Boolean results are sufficient; callers identify incompatible paths in errors.

No app metadata is included in views. View labels are the caller's assertion of semantic comparability, explicitly documented at comparison/equivalence. Within evidence aggregation app bundle/ability equality IS checked; do not retain the superseded suggestion to ignore it.

Reader verification makes metadata-only cross-view equivalence safe: each CSV already conforms to its own declaration, so equivalent metadata permits positional summary value comparison. No second, independent cross-view CSV identity policy is needed. Per-tag comparison continues to use tag identity and the union of tags.

### 6. Execution lifecycle and exclusive destination owner

The facade normalizes/validates input and creates `EvidenceStore` plus internal options; the runner's local initialization is the sole implementation owner of `dst.mkdir(parents=True, exist_ok=False)`. Do not also create it in the facade or CLI. Remove duplicate destination configuration if practical; otherwise populate options and store from the same normalized value. Remote roots remain independently host-timestamped under `/data/local/tmp` for each execution, not derived from local view names.

Capture expected files from `active_apps` BEFORE checking PIDs/capturing. The declaration and capture use the same active-app collection. Explicit `terminate=True` removes eligibility; a missing PID is fatal, not warning/removal/continuation. Preserve expected declarations for the failing event. Queue every successful capture immediately so a later capture error does not lose the receivable prefix. Screenshot expectation likewise precedes capture.

Preserve existing acquired-resource finalization: stop streams, write metadata (including empty categories), attempt all pending receives, and clean acquired remote storage; retain primary errors with secondary context via `CombinedError`. Do not invoke finalizers on an existing destination whose exclusive creation failed. Cleanup-only failure still raises. Local partial evidence is never removed. Structural validation runs after successful transfer/finalization before returning success; it must not replace a primary execution failure with a secondary missing-file error.

Preserve invocation-relative input resolution and the WSL/HDC relative receive-path workaround. The runner may continue changing cwd for execution, but must restore it on every exit. Remote log commands remain HDC `hilog` and ABD `logcat`; only artifact naming becomes `logs/log.log`. Update neutral diagnostics formerly saying hilog. Device operations, UI translations, health policy, and timeout values are not changed by this redesign.

### 7. Analysis, ordering, and mathematical contracts

Replace `generate_reports`/`average_reports` workflow boundaries with direct evidence analysis. Remove their intermediate raw-summary/breakdown readers and `_breakdown_pairs` filesystem discovery; retain readers for actual aggregate views. Keep named `AppSnapshotLabelPair` and existing mathematical helpers with adjusted inputs.

Each source yields filtered `SmapsSummary` values. A `None` parse result contributes no row/tag/plot point; a matching zero-valued mapping contributes a zero profile. Sources with zero rows remain in the outlier population and score zero. Preserve all nine `SUMMARY_METRICS` in order: size, rss, pss, referenced, shared, private, swap, swap_pss, anonymous (all `_kb`).

Keep trim fraction 0.20, minimum retained count 2, minimum retained fraction 0.60. Discard count is `max(0, min(floor(0.20*N), N-2, N-ceil(0.60*N)))`. Score each source by median absolute relative deviation of its present cells against cell medians across supplied samples; zero-median cells contribute zero and absent cells are ignored. Empty samples score zero. Sort candidates by descending score then ascending input index; discard the allowed quota even for tied/zero scores. Discard whole sources consistently across summary, breakdowns, plots, and audit.

Aggregate retained pair totals and retained tag values independently. Pair `n_samples` counts contributing retained sources; tag `n_samples` counts retained sources containing that tag. Do not zero-fill absent pairs/tags or sum aggregate tag statistics to construct summary statistics. Statistics order is mean, geomean, median, std, min, max. Std is sample deviation, zero for one contribution; any contributing zero collapses geomean to zero.

Construct ONE final ordered pair sequence by walking reference snapshot artifacts and files in their declared order and retaining pairs with retained contributions. All sources are structurally equivalent, so the first retained source is a suitable reference. Use this sequence for summary rows and breakdown declarations/writes. Replace `_union_keys` first-seen ordering. Sort tag rows by descending `size_kb_mean`, tag ascending on ties, independently of artifact ordering.

Summary header is `app_label,snapshot_label,n_samples` followed by metric-major/statistic-minor columns. Breakdown header substitutes `tag,n_samples`. Emit only retained `summary.csv`. The view manifest is derived from final results, not source coverage before filtering. Different raw tag contents can make otherwise equivalent evidence produce incompatible view layouts; preserve this accepted limitation.

### 8. Plotting and warnings

Consolidate single/raw and aggregate paths around aggregate rows and `ViewStore`. Boundary:

```python
def generate_averaged_plots(
    rows: Sequence[AveragedPlotRow],
    store: ViewStore,
    timestamps: Mapping[str, int],
) -> None: ...
```

Timestamp map comes from the already loaded first retained source. No reopening evidence in the renderer. Every plotted label must be present; missing timestamp raises. Empty rows/empty metadata are valid and yield no plots. Keep timestamp sorting with stable ties and origin at each app's first PLOTTED point. Keep geomean y values, std error bars (zero for one sample), headless SVG, dotted lines, rotated unwrapped labels, dynamic layout, grids and dense ticks. Remove raw/log rendering, `_log_value`, and stale log-plot requirements/tests.

For each parsed file returning `None`, log once PER create_view call: `smaps file {path} has no mappings aggregated for pattern {pattern}`. This covers empty text and no matches without claiming corruption. Include path and pattern even with default `.*`; parser stays pure. Per-iteration and aggregate views may legitimately warn again for the same evidence. Default `err` logging may suppress warnings.

### 9. Comparison consumes standalone views

Keep external `--base`, repeated `--change`, and `--out-dir` options, but inputs are exact view directories. Internal boundary:

```python
def compare_reports(base_path: Path, change_paths: list[Path], out_dir: Path) -> None: ...
```

Load `ParsedView` objects through the single strict reader, check `is_view_equivalent`, then compare median columns positionally. Preserve command-line change order, Decimal arithmetic, original numeric text, per-tag union ordering, blanks for unavailable tags, and blank percentage against a zero baseline. Remove `use_median` schema inference, run-root discovery, `summary_filtered.csv` selection, and breakdown directory scans. Validate all inputs before comparison output reservation. Exact comparison output must be new; once created it is also left partial on generation failure. Comparison results are not views and get no view manifest just to reuse code.

### 10. CLI helpers, names, and logging

All parsers (`run.py`, new wrappers, `compare.py`, and `record_hdc.py`) explicitly set `allow_abbrev=False`. Each CLI exposes/owns `--memmem-log-level` and `--memmem-log-file`, configures once at its exception boundary and resets in `finally`. Library operations neither configure nor reset the caller's logger. Keep error-prefixed `memmem error:` diagnostics/status 1 and argparse errors for bad syntax/regex. Device logs and memmem logs remain distinct. Recorder prompts, generated-file announcements and existing immediate conversion warnings preserve their user-facing printing behavior; the recorder's new logger options control application/backend diagnostics and fatal-operation reporting, not suppression of required conversion warnings. Runner/view recoverable-warning rules do not change recorder conversion semantics.

Private shared helper signatures in `src/cli_helpers.py` (not public `lib` exports):

```python
def add_logging_arguments(parser: argparse.ArgumentParser) -> None: ...
def add_execution_arguments(parser: argparse.ArgumentParser) -> None: ...
def add_view_arguments(parser: argparse.ArgumentParser) -> None: ...
def compile_smaps_filter(value: str) -> re.Pattern[str]: ...
def load_flow(path: Path) -> Flow: ...
def load_config(env_path: Path) -> Config: ...
def load_device(env_path: Path, backend: str) -> Device: ...
def execute_iterations(flow: Flow, device: Device, out_dir: Path,
                       repeats: int, *, reboot: bool, device_log: bool) -> list[Path]: ...
def view_destination(out_dir: Path, timestamp: str) -> Path: ...
```

Registration helpers must not register common options twice. Move the existing `Config` model (optional hdc_path/abd_path) and `load_config` from run.py into CLI helpers. `load_device` composes that loader; `record_hdc.py` also uses it to obtain an HDC path and construct the existing Hdc transport needed for start_shell/shell, rather than importing run.py or expanding Device. Logging registration performs no configuration at import/argument-registration time. `execute_iterations` creates a missing parent out-dir (existing parent accepted), writes canonical `flow.json` once, then calls `lib.execute_flow` with exact `iteration_i/evidence` destinations. No public repeat API is introduced. Avoid circular imports: `lib` never imports CLI helpers; helpers may call the facade. Preserve macro preprocessing, `$desc`, command-support checks, selected-backend-only `.env` requirements, positive repeats, and reboot before each iteration.

Implementation convention to protect existing parent contents: create `flow.json` exclusively and fail if it already exists, rather than silently overwriting provenance. Before device work, preflight intended evidence destinations and flow-file availability; APIs still perform exclusive creation to handle races. Existing unrelated parent content is allowed. No implicit resume or skipping old iterations. Only CLI wrappers write canonical flow; direct library execution needs no parent flow file.

`execute_flow.py` accepts `--flow`, `--backend {hdc,abd}` (default hdc), `--out-dir`, `--repeats` (default 1), mutually exclusive `--reboot`/`--no-reboot` (default false), mutually exclusive `--device-log`/`--no-device-log` (default true), and logging flags. `run.py` adds `--smaps-filter`; execution-only CLI does not accept it. Preserve run's timestamped default root and use the same default for execution-only CLI. `create_view.py DIR1 [DIR2 ...] --out-dir PARENT [--smaps-filter REGEX]` requires explicit output parent, has no device options or `.env` requirement, and accepts an existing parent. This is a concrete CLI default convention, not another API mode.

Capture one host timestamp per invocation with `%Y%m%d_%H%M%S_%f`, even with explicit output parents. Reuse it for run.py's individual/combined destinations. Standalone view calls capture fresh timestamps; collisions fail, never overwrite. `run.py` collects ALL repetitions before rendering any view. Collection failure stops subsequent repetitions and all rendering. Rendering failure stops subsequent views; preserve evidence and every partial/completed output. For one repeat still create both views. `summarize.py` and backup/restore recovery disappear.

### 11. Lexical normalization

`normalize(Path) -> Path` collapses lexical `.`/`..` using platform path semantics and preserves absolute/relative form; `absolutize(Path) -> Path` anchors a relative path to invocation cwd then normalizes. No `Path.resolve`, symlink traversal, inode deduplication, or parent-tree destination guard. Apply consistently to API inputs/returns, CLI flow/output/log/backend paths, comparison paths, provenance, and outliers. Symlink aliases may count as different sources; documented limitation. Resolve all caller-relative paths before the runner changes cwd. Artifact-relative paths follow their own portable schema.

### 12. Complete affected-area inventory

| Area | Required implementation/review work |
|---|---|
| `lib.py` | Replace run/export, preserve builders/preprocess/device/logger/public typing; avoid eager report imports in execution-only path. |
| `run.py` | Compose shared collection then views; single timestamp, no direct old report calls, no abbreviation. |
| `execute_flow.py`, `create_view.py` (new) | Thin independently logged wrappers with the contracts above. |
| `summarize.py` | Delete, with no compatibility redirect. |
| `src/cli_helpers.py` (new) | Share setup/options/naming/repeats; no new public facade API or import side effects. |
| `src/result.py` | Replace mixed store with EvidenceStore/ViewStore, exact shortened local/remote names and optional remote root. |
| `src/metadata.py` | Ordered nonempty file declarations, validation, root view metadata; shared existing pair identity if moved. |
| `src/evidence.py`, `src/view.py` (new) | Structural validator, ParsedEvidence/parser, ParsedView/strict aggregate reader, boolean equivalence; no extra shape entities. |
| `src/runner.py` | Exclusive evidence creation, eager empty metadata, move flow/report ownership out, final structural validation, finalizer guards, cwd/transfer preservation. |
| `src/commands.py` | Declare expected files first, fatal PIDs, incremental successful capture queue. |
| `src/report.py` | Direct parsed-evidence aggregation, retained-only schema, shared final order, aggregate manifest, normalized discarded paths, remove raw CSV inputs/scans. |
| `src/plot.py` | ViewStore and explicit timestamps, aggregate path only, strict missing timestamp, remove log transforms. |
| `src/smaps.py` | Compiled non-null default, keep parsing metrics/tag semantics and no global logging. |
| `src/path.py` | Normalize/absolutize split and lexical semantics. |
| `compare.py` | Strict view reader, metadata equivalence, aggregate medians, no discovery/legacy branching or failure cleanup; comparison result stays distinct. |
| `src/log.py`, `src/errors.py` | Review caller-owned logger and primary/secondary errors; preserve semantics unless wiring requires changes. |
| `record_hdc.py` | Remove run.py config import in favor of shared Config/load_config; preserve direct Hdc transport use, recording/conversion warnings/prompts; explicit no-abbreviation parser and independently owned logging flags. |
| `src/device.py`, backend device adapters and HDC/ABD wrappers | Preserve protocol, UI, process-handle and transfer semantics; review log path/diagnostic integrations and invocation-relative executables. |
| `test/mock/device.py`, `test/mock/{hdc,abd}.py` where present | Remove old log filename assumptions; expected declarations, fatal PID and partial capture/receive tests; preserve command fakes. |
| `test/{lib,run_cli,runner,commands,result,metadata,report,plot,smaps,compare,preprocess,log,device,hdc,abd}_test.py` | Migrate API, filenames, manifests, strict validation and changed lifecycle/order assertions. |
| `test/summarize_test.py` | Replace with execution/view CLI tests, not backup/restore tests. |
| New reader/equivalence/path/CLI tests; `test/fixtures/` and typed public-user fixtures | New-format round trips, edge cases, exported API mypy coverage, no sources opened by comparison. |
| `Makefile` | Replace explicit summarize formatter path; add both new wrappers; preserve test/mypy commands and note tests_full formats in place. |
| `README.md` | Rewrite setup-independent API usage, CLI defaults, output trees, filter flags, examples, comparison and failure/recovery guidance; no old format fallback. |
| Nine proposal capabilities | Apply deltas together; update cross-references and acceptance scenarios. |
| Existing `src/schema.py`, `src/preprocess.py`, `src/types.py` | Preserve label/macro/numeric contracts; reuse for metadata/CLI validation rather than alternate identity rules. |

Other local user analysis scripts may consume old CSV layouts; no automatic rewriting of untracked/private scripts. Document the intentional format break. No new dependencies are required.

## Risks / Trade-offs

- First-seen ordering or directory scans accidentally survive -> regression tests with early no-match rows and undeclared extras; manifest order is authoritative.
- App identity omitted from view -> explicitly document caller semantic responsibility; compare metadata only, never reopen evidence.
- Valid evidence is only structurally complete -> comments/specs state no whole-flow proof or content-integrity guarantee; keep follow-up scope separate.
- Failed outputs remain visible -> readers validate declarations and required CSVs; caller tracks failure; no false atomicity claim.
- Empty samples and tied scores still discard quota -> preserve mathematical tests; do not silently drop empty samples before scoring.
- Multiple views reparse evidence and may duplicate no-match warnings -> accepted simplicity, warning frequency per call; no cache/provenance invalidation system.
- Some current specs contain unrelated drift (e.g. thermal threshold) -> do not change health policy while reconciling reporting/CLI contracts. Remove stale raw log-plot requirements explicitly.
- New parent reuse permits unrelated existing files -> exact API destinations and root canonical flow remain exclusive; no implicit resume or destructive overwrite.
- No symlink resolution/ancestry guard -> document lexical duplicate detection and caller-owned destination separation, without expanding scope.

## Migration Plan

This is a coordinated breaking release: implement schemas/readers/stores, execution boundary, analysis/view writer, facade/CLIs, comparison, and tests/docs together. Remove old lib.run, summarize, CSV aggregation/discovery and log plots. Do not auto-migrate old outputs or leave compatibility shims. Old outputs remain untouched and are rejected by new readers. Rollback means reverting the code/spec change and using old-format data with the old version; new outputs must not be silently interpreted by old tools. No device operation is needed to verify the change: use fakes and synthetic/checked-in smaps fixtures.

Validation after implementation: targeted unit modules, complete `make test`, `make mypy`, and intended formatting checks. `make tests_full` invokes in-place autopep8 before tests/mypy; do not describe it as read-only. Planning validation uses OpenSpec strict validation, archive-preview consistency where available, and independent scope audit only; it does not execute benchmarks or implement these interfaces.
