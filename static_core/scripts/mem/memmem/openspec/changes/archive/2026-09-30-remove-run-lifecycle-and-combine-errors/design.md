# Design

## Context

`src.runner` currently carries `_RunLifecycle` booleans across pre-flow and post-flow phases to decide whether local evidence finalization and remote cleanup are safe. It also has `_PostFlowErrors` plus several independent first-error/later-error branches. See `proposal.md` for motivation.

The public `lib.run()` API already reserves the requested output directory before device actions. A single run passes that precreated directory into the runner, while repeated runs reserve a top-level directory and let each runner call create its iteration directory. The `local_root_precreated` parameter exists only to support that difference.

The observable contracts remain unchanged: any existing output directory is rejected, metadata follows successful local initialization, all staged artifacts are received before remote cleanup, a partially initialized remote root is cleaned up, and the first failure remains the raised exception's cause.

## Goals / Non-Goals

**Goals:**

- Remove `_RunLifecycle`, `_PostFlowErrors`, runner-local `_combine_errors`, and precreated-local-root plumbing.
- Express local and remote resource ownership through setup success and lexical execution scopes.
- Provide `src.errors.CombinedError` as the single internal error accumulator used by runner failure paths.
- Retain current output layout, cleanup order, exception chaining, and public `lib.run()` API behavior.

**Non-Goals:**

- Allow existing empty output directories or otherwise change output-directory reservation semantics.
- Add remote directory probing to `Device`.
- Export `CombinedError` from `lib.py`, add dependencies, or change device/CLI APIs.
- Change user-visible error text except where the existing aggregation pattern already supplies contextual text.

## Decisions

### Always create each runner output root in the runner

`run_benchmark()` will always create `store.local_out_dir()` with `exist_ok=False`; it will no longer accept `local_root_precreated`. `lib.run()` will call the runner directly for a single run. For repeated runs, it will still reserve the top-level output directory first, then invoke the runner for each not-yet-existing iteration directory.

This preserves atomic reservation and removes the runner's conditional root-creation branch. Allowing an existing empty output directory was rejected because it weakens exclusive reservation and conflicts with the documented existing-directory rejection contract.

### Split local and remote setup at successful acquisition boundaries

Local initialization will complete before the runner enters the execution/finalization path. After it succeeds, local finalization always attempts child shutdown, metadata writing, and staged-artifact receipt, even when preparation or flow execution fails. If local initialization itself fails, that path is never entered.

Remote initialization will create the remote root first. If creating a later remote subdirectory fails, the initializer immediately attempts to remove the already-created root and combines any cleanup failure with the setup failure. Once remote initialization returns, the caller always performs remote cleanup after local finalization.

This retains the lifecycle guarantees without querying directory existence or passing mutable ownership flags. A context manager was considered, but rejected because its natural exit point risks remote cleanup before local metadata/artifact finalization.

### Use an internal public-named `CombinedError` accumulator

`src.errors.CombinedError` will have this module-facing API:

```python
class CombinedError:
    def __init__(self) -> None: ...
    def add(self, exc: Exception, context: str | None) -> None: ...
    def get(self) -> Exception | None: ...
    def raise_if_any(self) -> None: ...
```

The first `add()` records both `_original` and `_combined`. Later calls update `_combined` using the private `_combine_errors()` method. `raise_if_any()` returns when no error was added; otherwise it asserts a combined error exists and raises it from `_original`.

`CombinedError` is public by name within `src.errors` so all runner helpers can use it, but it is not re-exported through `lib.py` and does not become part of the external library API. The accumulator replaces manual error aggregation in benchmark execution, AppFlow cleanup, post-local finalization, child shutdown, and artifact receipt.

## Risks / Trade-offs

- [Restructured branches omit a finalizer] -> Preserve and extend failure-path tests for local setup failure, pre-remote failure, partial remote setup, and cleanup after flow failure.
- [Remote cleanup failure masks setup or flow failure] -> Use `CombinedError` at each boundary and retain `raise combined from original`.
- [Repeated-run top-level reservation regresses] -> Keep `lib.run()` top-level `mkdir(exist_ok=False)` for repeats and test that pre-existing trees remain untouched.
- [Refactor changes diagnostics unexpectedly] -> Keep existing context strings at all current aggregation boundaries and test message fragments plus exception causes.

## Migration Plan

1. Add `src.errors.CombinedError` and focused unit tests.
2. Refactor output ownership and runner finalization while keeping current external behavior tests green.
3. Run the full unit and type-check suites.

Rollback is a source-only revert; no persisted data, command syntax, or output format changes are introduced.
