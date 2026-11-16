# Proposal

## Why

CLI tests patch the process-global `sys.argv`, obscuring invocation inputs and coupling tests to interpreter state.

## What Changes

- Let CLI `main()` functions accept an optional argument list.
- Preserve production behavior when no list is supplied.
- Replace `sys.argv` test patches with explicit argument lists.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `testing-support`: Make CLI entrypoint invocation deterministic without process-global argument mocks.
