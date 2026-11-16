# Tasks

## 1. Library-owned evidence provenance

- [x] 1.1 Add the required strict evidence metadata model (`flow` fixed to `flow.json`, `reboot`, `device_log`) and EvidenceStore root-file accessors; verify metadata tests reject missing/extra fields, wrong filenames, and coerced booleans, and store tests verify exact paths.
- [x] 1.2 Pass canonical flow into local runner initialization and eagerly write evidence flow/metadata before device preparation; verify runner and direct-library tests cover actual option values, preserved descriptions/expanded commands, empty flows, existing-destination rejection, and retained provenance on preparation/collection failure.
- [x] 1.3 Extend evidence validation with a named result containing flow and metadata, retain both in ParsedEvidence, and update all evidence fixtures/direct constructors; verify malformed/missing provenance is rejected, copied evidence works without a parent, and differing valid provenance does not change equivalence. Run evidence, report, plot, runner, and integration tests to catch fixture regressions together.
- [x] 1.4 Remove run-root flow writing and its run-marker collision check from execute_iterations while retaining all intended destination checks; keep command support validation solely in lib.execute_flow. Update CLI tests to verify no run-root provenance, evidence-local canonical copies, unrelated parent flow preservation, and retained successful/partial prefixes. Update README evidence layout and compatibility/completion statements and verify examples match generated output.

## 2. Hook context and subprocess protocol

- [x] 2.1 Define exact execution/view context serialization with fixed event values, canonical Flow JSON, normalized paths, and effective regex flags; verify context tests cover both events for each operation, source order, optional description, and absence of device/backend/repeat/view-kind fields.
- [x] 2.2 Implement the shared phase runner with one closed temporary JSON file per nonempty phase, ordered duplicate-preserving invocation through subprocess.run, inherited stdout/stderr, captured cwd, and timeout=300 per hook; verify with executable fixtures and an injected runner that argv/cwd/timeout are correct, empty phases allocate nothing, and temporary contexts are removed on success/failure without reading edits into runtime inputs.
- [x] 2.3 Add informative exception wrapping for nonzero exits, timeouts, launch errors, and generic process-runner exceptions, plus info-level start and successful-end logging only; verify failure diagnostics include event/path/cause but omit destination, later hooks stop, return codes and timeout duration are present, logging respects configuration, and cleanup does not mask primary failures. Simulate timeout without waiting five minutes.
- [x] 2.4 Document executable/shebang permissions, JSON-file argv protocol, Python and Bash logging hooks, inherited output, per-hook timeout, read-only context, and descendant-cleanup responsibility; verify the examples run against representative context fixtures using existing prerequisites.

## 3. CLI helper and entrypoint integration

- [x] 3.1 Add the four repeatable options to the existing execution/view argument helpers; verify parser tests for applicable options per CLI, omission defaults, duplicates, ordering, and rejection of irrelevant/abbreviated options.
- [x] 3.2 Add private _execute_flow_with_hooks and create_view_with_hooks in cli_helpers, use list-based hook parameters, normalize execution paths once in execute_iterations, and capture cwd inside each operation helper; verify exact pre/library/post traces, skipped post hooks on library failure, unchanged public lib signatures, context non-mutation, and correct cwd/path behavior around execution's cwd restoration using fakes/injected seams.
- [x] 3.3 Thread execution hooks through execute_iterations and both execution entrypoints, and route all standalone/individual/combined view calls through the view helper; verify two-repeat trace ordering, duplicates, unchanged timestamps/filter/plots, pre-hook failure preventing the library call, and post-hook failure preserving output while stopping later iterations/views. Verify hooks precede optional reboot and direct library calls never invoke hooks.
- [x] 3.4 Update README CLI usage and lifecycle diagrams with repeatable registrations and fail-fast behavior; verify documented options against parser help and example traces, including no hook provenance or changed view metadata.

## 4. Integrated acceptance

- [x] 4.1 Exercise device-free collection-to-view integration with FakeDevice and executable hook fixtures; verify self-contained required evidence provenance, per-phase context cleanup, hook ordering, unchanged view schema/equivalence, and retained outputs on failure.
- [x] 4.2 Run `make pylint`, `make mypy`, `make test`, `git diff --check`, and `openspec validate add-cli-operation-hooks --strict`; resolve failures and record successful validation before marking implementation complete.

## Validation results

- `make pylint`: passed.
- `make mypy`: passed, 69 source files.
- `make test`: passed, 320 tests (including executable Python/Bash examples and device-free hook integration).
- `git diff --check`: passed.
- `openspec validate add-cli-operation-hooks --strict`: passed.
- Timeout behavior tested through an injected subprocess runner; no five-minute wait or connected-device operations were needed.
