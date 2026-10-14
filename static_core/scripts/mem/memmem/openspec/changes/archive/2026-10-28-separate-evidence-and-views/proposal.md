# Proposal

## Why

Memory evidence collection, single-run reports, repeated-run aggregation, and recovery currently share storage and CSV-based intermediates. This prevents independent, repeatable views of the same evidence and makes adding post-hoc smaps filtering destructive or inconsistent. Separate collection from metadata-driven analysis so every new view has an explicit source list, filter, artifact contract, and uniform statistical schema.

## What Changes

- **BREAKING** Replace `lib.run` with `execute_flow(flow, device, dst, *, reboot=False, device_log=True) -> Path` and `create_view(dirs: list[Path], dst: Path, smaps_filter: Pattern[str] = MATCH_ALL) -> Path`. Exact API destinations must be new; return normalized absolute paths.
- **BREAKING** Add `execute_flow.py` and `create_view.py`; remove `summarize.py`. `run.py` collects all repetitions first, then creates individual and combined views through the same APIs. Share CLI setup in `src/cli_helpers.py`; all CLIs explicitly disable argument abbreviation and own logging configuration.
- **BREAKING** Always use `iteration_N/evidence`, including one repeat; persist canonical `flow.json` once at the CLI run root, with no run-root metadata. CLI parent output directories may already exist; exact evidence/view destinations may not.
- **BREAKING** Use `apps.json`, optional `logs/log.log`, eager snapshot/screenshot directories and metadata, and `<snapshot>/<app>.smaps` names. Ordered artifact metadata declares nonempty expected file lists before capture. Missing expected PIDs are fatal; successful capture prefixes remain receivable.
- **BREAKING** Generate new `view/view-<timestamp>` directories with retained-only aggregate `summary.csv`, `outliers.csv` headed `discarded`, aggregate breakdowns plus their ordered manifest, plots, and source/filter metadata. Use the same schema for one or multiple sources. Keep current statistics, no-match omission, outlier selection, and first-plotted-point timing; remove log-transformed plots.
- Introduce minimal `EvidenceStore`, `ParsedEvidence`, `ViewStore`, and `ParsedView` responsibilities, strict readers, and reusable metadata-based evidence/view equivalence predicates. Analyze declared raw files directly, without intermediate report CSVs or directory discovery.
- **BREAKING** Compare explicit views using their validated metadata and aggregate medians, without opening evidence. Reject legacy formats. Leave new partial outputs on failure; no backup/restore, staging, or cleanup.

## Capabilities

### New Capabilities

None; extend the existing capabilities rather than creating overlapping reporting concepts.

### Modified Capabilities

- `programmatic-interface`: split APIs, exact destinations, return values, and preserved builder/device helpers.
- `benchmark-flow`: CLI orchestration, ownership, repeats, injected dependencies, lexical path normalization, and fatal expected-PID handling.
- `result-evidence`: evidence/view layouts, ordered manifests, structural validation, direct aggregation, uniform statistics, equivalence, and failure semantics.
- `smaps-analysis`: mandatory compiled string pattern with match-all default, preserving parsing semantics.
- `result-comparison`: explicit validated views, metadata equivalence, aggregate-only reading, and partial-output retention.
- `memmem-logging`: independent CLI ownership, new exception boundaries, and consistent no-match warnings.
- `log-streaming`: backend-neutral evidence path with existing stream lifecycle preserved.
- `device-abd`: move unsupported-command validation to `execute_flow`.
- `testing-support`: migrate old facade/report tests and add the full reader, equivalence, ordering, failure, and CLI matrix.

## Impact

Affected production files include `lib.py`, `run.py`, `summarize.py` (removed), `compare.py`, `record_hdc.py` (CLI abbreviation consistency), new `execute_flow.py`, `create_view.py`, `src/cli_helpers.py`, storage/metadata/reader modules, `src/{result,runner,commands,report,plot,smaps,path,log,errors}.py`, and backend path/diagnostic integration. `Device` transport and UI command behavior remain intact. Tests, fake devices, typed facade fixtures, `Makefile`, `README.md`, and the nine capability specifications must change together. The design inventories every affected area and exact interfaces.

No new production dependencies, device commands, compatibility adapters, evidence-completion manifests, content-integrity checks, automatic recovery, symlink resolution, or atomic publication are introduced. Structural validity is checked; whole-flow completion remains caller responsibility. Planning artifacts only are created by this change proposal.
