# Proposal

## Why

Pydantic models repeat the same strict extra-field configuration throughout the
schema and metadata layers. A shared base makes the default explicit and gives
future configuration changes one controlled definition.

## What Changes

- Add one common Pydantic model base with the current `extra="forbid"` default.
- Migrate all project Pydantic models to inherit the common base directly or
  through an existing project model base.
- Remove redundant strict-extra declarations while preserving model-specific
  aliases and validation configuration.
- Explicitly configure recorder event models to ignore unknown input fields.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This change preserves existing validation behavior and does not change
the public behavior contract.

## Impact

- Affected code: `src/schema.py`, `src/metadata.py`, `record_hdc.py`, and a
  new shared helper under `src/`.
- Affected tests: schema, metadata, and recorder parsing coverage.
- No public API, dependency, or persisted-format changes.
