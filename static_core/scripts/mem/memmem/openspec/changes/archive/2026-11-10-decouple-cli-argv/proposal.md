# Proposal

## Why

Optional CLI arguments retain a process-global `sys.argv` fallback, so entrypoint behavior is not fully isolated from interpreter state. Requiring explicit argument lists makes direct invocation deterministic while keeping process argument access at the executable boundary.

## What Changes

- **BREAKING** Require `argv: list[str]` for every memmem CLI `main` function.
- Make each `__main__` block pass `sys.argv[1:]` to its `main` function.
- Update all direct CLI tests to pass explicit argument lists; remove no-argument entrypoint calls.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `testing-support`: CLI entrypoints and their tests use required explicit argument lists.

## Impact

Updates `run.py`, `execute_flow.py`, `create_view.py`, `compare.py`, `record_hdc.py`, and their direct-call tests. Subprocess CLI behavior and dependencies are unchanged.
