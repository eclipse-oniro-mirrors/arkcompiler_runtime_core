# Proposal

## Why

`pydantic.Strict*` annotations preserve runtime validation but are inferred as `Any` by mypy in this project. This weakens static checking for model fields and leaves strictness inconsistent across models that otherwise share the same base class.

## What Changes

- Configure the shared `PydanticModel` base class to use Pydantic strict validation by default.
- Replace `pydantic.Strict*` field annotations with builtin Python types throughout Pydantic models so mypy retains precise field types.
- **BREAKING** Reject coercible Python values for every builtin-typed field on a `PydanticModel`, rather than converting them during validation.
- Preserve valid JSON metadata and flow round trips, including JSON path representations accepted by Pydantic strict JSON validation.
- Add validation coverage for strict default behavior and existing metadata, flow, and public-builder contracts.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `benchmark-flow`: Flow validation rejects coercible values for builtin-typed flow fields before device actions.
- `programmatic-interface`: Public flow-builder wrappers retain precise builtin field types and reject coercible arguments during immediate validation.
- `result-evidence`: Evidence and view metadata readers reject coercible Python values while continuing to accept valid serialized JSON metadata.

## Impact

- Affected code: `src/pydantic_helpers.py`, `src/schema.py`, `src/metadata.py`, and their tests.
- Affected behavior: direct Python model and public-wrapper construction no longer coerces builtin values, such as integer values for boolean fields or numeric strings for integer fields.
- Dependencies: no additions; uses the pinned Pydantic 2.13.4 `ConfigDict(strict=True)` behavior.
