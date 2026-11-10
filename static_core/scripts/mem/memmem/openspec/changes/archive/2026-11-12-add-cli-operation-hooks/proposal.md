# Proposal

## Why

Users need to run custom actions, such as replacing a device library or logging results, around individual collections and view generation without reimplementing CLI orchestration. Evidence also needs to retain its executed canonical flow and execution settings independently of the CLI run root.

## What Changes

- Add four repeatable CLI hook options for pre/post execution and pre/post view creation, with ordered executable invocation and JSON context files.
- Centralize hook/API ordering in CLI helpers. Public library signatures remain unchanged and library operations never invoke or record hooks.
- Invoke each executable directly with a context-file argument, inherited output, invocation cwd, and a 300-second per-hook timeout. Fail fast with informative exceptions; log hook starts and successful ends at info level without dedicated failure logs.
- **BREAKING** Move canonical `flow.json` from the CLI run root into every evidence destination, including evidence created directly through the library.
- **BREAKING** Require evidence `metadata.json` containing exactly `flow: "flow.json"`, `reboot: bool`, and `device_log: bool`; readers parse this metadata and the canonical flow. Equivalence ignores both.
- Preserve view metadata, collection-before-rendering order, and partial-output retention. Store no hooks, repeats, or backend in metadata.

## Capabilities

### New Capabilities

- `cli-operation-hooks`: Registration, process execution, contexts, ordering, and failure behavior of CLI-only operation hooks.

### Modified Capabilities

- `benchmark-flow`: Repeated execution stores flow provenance inside each evidence destination rather than at the run root.
- `result-evidence`: Library-owned evidence initialization writes required provenance; readers validate and retain it without changing equivalence.

## Impact

- CLI integration: `run.py`, `execute_flow.py`, `create_view.py`, and `src/cli_helpers.py`.
- Evidence lifecycle: `src/runner.py`, `src/store.py`, `src/metadata.py`, and `src/evidence.py`; direct-library contracts are preserved at the signature level while output layout changes.
- Tests: CLI orchestration, subprocess protocol, evidence reader/runner, direct-library integration, and fixtures constructing `ParsedEvidence`.
- Documentation: README CLI examples, evidence layout, executable Python/Bash hook examples, and failure semantics.
- No new production dependencies. Older evidence without the new provenance is intentionally rejected; existing view metadata is unchanged.
