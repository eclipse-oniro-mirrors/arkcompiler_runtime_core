# Proposal

## Why

Footprint reporting duplicates its metric schema and source-value derivation in report generation, unlike summary metrics which are defined by a reusable dataclass.

## What Changes

- Add a reusable `Footprint` dataclass for derived footprint source values.
- Infer `FOOTPRINT_METRICS` from the dataclass fields.
- Centralize conversion from `MemProfile` to `Footprint` and remove hardcoded footprint metric mappings from report generation.
- Define memdump category order and tag-matching patterns together, and derive classification from that ordered source.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `result-evidence`: Preserve the existing footprint CSV contract while deriving its metric schema from the footprint data model.
- `result-evidence`: Preserve memdump category precedence, CSV order, and the `other` fallback while centralizing category patterns.

## Impact

- `src/profile.py` owns both source-value dataclasses.
- `src/report.py` aggregates footprint profiles through the shared metric iteration pattern.
- `src/report.py` owns one ordered category-to-pattern mapping for memdump classification and output ordering.
- Existing footprint CSV layout and comparison behavior remain unchanged.
