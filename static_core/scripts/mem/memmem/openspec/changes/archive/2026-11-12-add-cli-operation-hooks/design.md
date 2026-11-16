# Design

## Context

See proposal.md for motivation. The inspected call graph has one collection seam (`src/cli_helpers.py:execute_iterations`) used by both execution CLIs, but view calls currently occur directly in `run.py` and `create_view.py`. `execute_iterations` writes run-root flow provenance before a collection list comprehension. `lib.execute_flow` revalidates a canonical flow, constructs `BenchmarkOptions`, and passes both to `run_benchmark`. `_pre_flow` already receives both materials, while `_initialize_local_output` receives only store/options. `run_benchmark` restores cwd before returning and calls `validate_evidence` on success.

`write_view` exclusively creates its destination and writes metadata during initialization before reports/plots; failures retain partial output. Evidence currently has eager app/artifact manifests but no execution provenance. `validate_evidence` returns a two-item tuple, and `ParsedEvidence` stores apps, snapshots, source, and measurements. Its consumers include report fixtures and direct constructor calls. `get_logger().info` obeys configured level and file routing and flushes messages; default level is `err`.

## Goals / Non-Goals

**Goals:**
- Put all CLI operation/hook sequencing through shared helpers while preserving atomic public library signatures.
- Derive hook context from the exact normalized operation arguments; derive evidence provenance from the validated flow and execution options.
- Make subprocess behavior deterministic and failures actionable without capturing hook output.
- Keep evidence self-contained and preserve established failure/output ownership rules.

**Non-Goals:**
- Python plugin imports, shell command strings, callback return-value protocols, async hooks, or hook-driven mutation of memmem runtime state.
- Hooks inside reboot/preparation or app-command execution, failure hooks, view-kind discrimination, or whole-process-tree termination guarantees.
- Hook/backend/repeat provenance, changes to view metadata, legacy evidence migration, or flow-to-artifact completeness proofs.

## Decisions

### 1. Shared CLI adapters, no public library hook API

Add `_execute_flow_with_hooks` and `create_view_with_hooks` in `src/cli_helpers.py`. The execution wrapper is private to the iteration orchestrator. Both accept required keyword-only pre/post hook lists populated by the CLI parser. These are not new facade exports.

Proposed signature shape (planning sketch):

```python
_execute_flow_with_hooks(flow, device, dst, *, reboot, device_log,
                         pre_hooks, post_hooks)
create_view_with_hooks(dirs, dst, *, smaps_filter, plots,
                       pre_hooks, post_hooks)
```

Both operation helpers capture `Path.cwd()` at entry, once for their pre/post phases. The execution library restores cwd before returning, and the view library does not change it. `execute_iterations` supplies normalized destination and hook paths to the private execution helper; the private helper does not normalize them again. The view helper normalizes its own sources/destination and hooks. All paths use lexical invocation-relative normalization before pre hooks, without resolving symlinks.

`add_execution_arguments` and `add_view_arguments` explicitly declare each hook argument using append semantics without a phase-generation loop. Normalize registrations before operation loops; preserve duplicates. A shared hook-phase runner can live in a focused internal module (`src/hooks.py`) to keep `cli_helpers.py` within existing complexity limits. It accepts the normalized paths, context, and captured cwd, independent of CLI parsing.

Replace the collection comprehension with an explicit loop. Remove run-root flow creation and its collision check; retain parent-directory creation and checking every intended evidence destination before the first iteration. Command support validation remains solely in `lib.execute_flow`, immediately before it reserves an evidence destination. An unrelated existing parent `flow.json` is untouched, not treated as a run marker.

Route all three CLI view call sites (individual and combined calls in `run.py`, standalone call in `create_view.py`) through the view helper. Retain current source order, timestamp reuse, plots/filter settings, logger setup/reset, and exception-to-exit-code behavior.

```text
run / execute_flow CLI
  -> execute_iterations
       -> execution helper for each repeat
            -> pre phase -> lib.execute_flow -> post phase

run / create_view CLI
  -> view helper for each planned view
       -> pre phase -> lib.create_view -> post phase
```

Alternative rejected: putting callbacks or hook provenance in public library parameters couples atomic operations to CLI policy. Generic event-only registration was also rejected in favor of independent phase options; the event remains useful in context for shared executables.

### 2. Fixed contexts, serialized as UTF-8 JSON files

Use typed internal context models or equivalent explicit serializers, with these exact wire shapes:

```json
{
  "event": "pre_execute_flow",
  "flow": {"flow": []},
  "dst": "/absolute/run/iteration_0/evidence",
  "reboot": false,
  "device_log": true
}
```

Execution event is `pre_execute_flow` or `post_execute_flow`. Flow serialization preserves `$desc` when present and expanded canonical commands. The context contains no device object.

```json
{
  "event": "pre_create_view",
  "dirs": ["/absolute/run/iteration_0/evidence"],
  "dst": "/absolute/run/iteration_0/view/view-T",
  "smaps_filter_pattern": ".*",
  "smaps_filter_flags": 32,
  "plots": true
}
```

View event is `pre_create_view` or `post_create_view`; pattern and flags come from the actual compiled pattern. There is no iteration, repeats, backend, or view-kind field. Pre/post contexts use the same operation arguments but different events.

For each nonempty phase, write and close one temporary JSON file before invoking any executable; keep it available for that phase's ordered callbacks, then clean it up on success or failure. A new file is prepared for the post phase, only after library success. Empty hook lists allocate no files. Temporary paths are not inside output destinations, which must remain exclusively owned by library creation.

Context edits are not read back. User hooks must treat the shared file as read-only; this is a contract, not a sandbox. Actual runtime inputs remain independently held in memory. Context-creation failure prevents that phase and its dependent operation. Cleanup failure must not mask an already-active hook or library error; use existing combined-error conventions where needed.

Alternative rejected: stdin JSON would work, but the agreed file-argument protocol is equally usable from Bash and Python and makes inspection straightforward.

### 3. Executable invocation and errors

Invoke synchronously using `subprocess.run([str(path), str(context_path)], cwd=captured_cwd, check=True, timeout=300)`. Do not configure stdout/stderr pipes, a shell, or interpreter inference. No new environment-variable protocol is introduced. Scripts need executable permission and a valid shebang. Each invocation gets its own 300 seconds, independent of phase length.

Wrap launch errors, nonzero exits, timeouts, and generic exceptions from an injected process runner with event, executable path, and cause. Preserve exception chaining. A timeout identifies 300 seconds; a nonzero exit includes its return code. No subsequent hook or library operation runs after failure. A post-hook failure does not invalidate or delete already-produced output.

Call `get_logger().info` before each invocation and after successful return, identifying event, path, and phase index so duplicates are distinguishable. Omit destination from hook diagnostics. Do not emit a separate hook-failure/end-on-failure log: the informative exception is surfaced by the existing CLI error handler. Info messages obey existing filtering/file routing, while child stdout/stderr remain inherited.

The timeout terminates/waits for the direct child through standard subprocess behavior; descendant cleanup belongs to the hook. Do not add process-group management in this change.

### 4. Evidence provenance is initialized by the runner

Add a strict evidence metadata model with required fields `flow: Literal["flow.json"]`, `reboot: bool`, and `device_log: bool`. The literal is preferable to an unrestricted Path because only one relative file reference is allowed. Unknown fields are forbidden by the base model. Add `EvidenceStore` accessors for root flow and metadata paths.

Change `_initialize_local_output(store, options)` internally to accept `flow`; `_pre_flow` already has it. Serialize the validated flow actually passed from `lib.execute_flow`, not an original unprocessed input file. Preserve `$desc` aliases and canonical expanded commands. Write root `flow.json` and `metadata.json` during local initialization, before device preparation, alongside eager app/artifact metadata. The metadata values derive solely from `BenchmarkOptions`.

```text
lib.execute_flow (signature unchanged)
  -> Flow.model_validate(f.model_dump())
  -> command-support validation
  -> run_benchmark(validated_flow, options, store, device)
       -> _pre_flow
            -> initialize local output including flow/metadata
            -> prepare device / collect / finalize
```

This matches view initialization ownership: exclusive destination creation followed by eager metadata and later work. Successful initialization leaves provenance even if preparation or collection fails. Failure during initialization can leave only a prefix; do not promise atomic publication, rollback, or a complete provenance file after a failed write. Pre hooks must not create the library destination.

Remove run-root provenance rather than duplicate it. A pre-hook failure may leave an empty run parent. Existing evidence targets remain protected from overwrite. Direct library calls gain the same evidence files without any new parameters.

### 5. Reader integration and equivalence

Extend `validate_evidence` to load the strict metadata and canonical Flow, as well as existing manifests. Introduce a named validation-result dataclass instead of expanding the current anonymous tuple; it carries metadata, flow, apps, and snapshots. `parse_evidence` consumes it and populates required `ParsedEvidence.metadata` and `ParsedEvidence.flow` fields. Update all direct constructors/fixtures, including `test/report_test.py` and `test/plot_test.py`.

Reject absent/malformed root files, extra metadata fields, wrong bool types, wrong flow reference, and invalid canonical flow. No fallback to parent `flow.json`, no optional provenance, and no migration shim. `is_evidence_equivalent` continues comparing app identities and ordered snapshot declarations only; metadata and canonical flow are retained for inspection but excluded from equivalence. Parsing a valid flow does not prove it was completed or require comparing every command to captured artifacts.

### 6. User-facing examples and verification seams

README examples should show executable scripts invoked with the context path as argv[1]. Python example:

```python
#!/usr/bin/env python3
import json
import pathlib
import sys

context = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
print(f"{context['event']}: {context['dst']}", flush=True)
```

Bash example (uses the existing Python prerequisite for JSON parsing):

```bash
#!/usr/bin/env bash
set -euo pipefail
event=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["event"])' "$1")
dst=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["dst"])' "$1")
printf '%s: %s\n' "$event" "$dst"
```

Show `chmod +x` and repeatable registration, including reuse of one executable at different phases. Explain that pre-execution hooks run before reboot, metadata is configuration not completion, and hooks own external side effects and descendant cleanup.

Prefer fakes/injected execution seams over new `unittest.mock` usage. Exercise actual subprocess argument/context/cwd behavior with small temporary executable fixtures where useful; simulate timeout through an injected runner that raises `TimeoutExpired` rather than waiting five minutes. Verify that production passes timeout=300 and leaves stdout/stderr inherited. Existing FakeDevice supports device-free end-to-end evidence creation.

## Risks / Trade-offs

- [Older evidence becomes unreadable] -> Intentional breaking format update; update fixtures and documentation together, and collect fresh evidence rather than infer missing configuration.
- [Arbitrary hook side effects persist] -> Fail fast, retain output, and document that no rollback or revalidation after post-hook mutations is promised.
- [Shared context can be altered by a hook] -> Document read-only use; never adopt edits into memmem arguments. No sandbox guarantee.
- [Five-minute timeout does not control descendants] -> Keep the agreed subprocess contract and assign descendant cleanup to scripts.
- [Run-root marker removal changes reuse behavior] -> Preserve all intended evidence-destination checks and explicitly test empty-parent reuse and unrelated flow-file preservation.
- [Failure diagnostics obscured by cleanup] -> Preserve the original error when temporary-file cleanup fails, consistent with existing error handling.

## Migration Plan

Implement required evidence writing/reading together with updated fixtures, then the hook protocol and CLI wiring. Update README layout and hook examples alongside their implementation groups. Run focused tests and project lint/mypy/full tests; validate OpenSpec artifacts. No automatic migration of old evidence or main-spec sync occurs during implementation. Rollback must revert writer, reader, CLI layout, and fixtures as a coherent unit; retained output must not be deleted by rollback tooling.
