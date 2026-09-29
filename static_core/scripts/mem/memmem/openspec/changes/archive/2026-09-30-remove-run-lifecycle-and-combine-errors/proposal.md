# Proposal

## Why

The runner represents local and remote resource ownership through mutable lifecycle flags and repeats error-combination logic across several execution paths. Replacing those mechanisms with lexical setup/finalization boundaries and one reusable error accumulator will make failure handling easier to reason about without changing benchmark behavior.

## What Changes

- Remove `_RunLifecycle` and the `local_root_precreated` plumbing from the runner and public orchestration internals.
- Keep rejection of every pre-existing output directory and preserve atomic output reservation for single and repeated runs.
- Restructure local and remote setup so successful setup directly determines which finalizers run, including immediate cleanup of a partially initialized remote root.
- Add `src.errors.CombinedError` to preserve the original exception while collecting later failures with contextual messages.
- Replace runner-local error aggregation helpers with `CombinedError` and add focused coverage for its behavior and lifecycle failure paths.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This change preserves existing output reservation, finalization, cleanup, and error-reporting behavior.

## Impact

- Affected code: `lib.py`, `src/runner.py`, new `src/errors.py`, and runner/error unit tests.
- Internal runner signatures lose precreated-output flags; the public `lib.run()` signature is unchanged.
- No new dependencies, device APIs, CLI options, output formats, or specification changes.
