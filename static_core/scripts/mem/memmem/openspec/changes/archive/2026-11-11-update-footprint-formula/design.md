# Design

## Context

`MemProfile` stores smaps `Pss` and `SwapPss` independently. `Footprint` is
the single ordered source-value model used by report generation, strict view
CSV validation, and comparison output. See `proposal.md` for motivation and
the specification deltas for observable behavior.

## Goals / Non-Goals

**Goals:**
- Represent the framework-compatible footprint and the former PSS-plus-SwapPss
  value as explicit derived metrics.
- Preserve one ordered metric source for report writers, readers, and
  comparison output.
- Update focused tests for derived values and CSV schemas.

**Non-Goals:**
- Change smaps parsing, raw summary metrics, or memdump metrics.
- Support reading footprint CSVs written with the former schema.
- Change comparison arithmetic or view equivalence rules.

## Decisions

### Expand the ordered `Footprint` model

Add `pss_plus_swap_pss` between `footprint` and `private_clean_kb`, and derive
the metrics in one function. `footprint` becomes `pss_kb + 2 * swap_pss_kb`;
`pss_plus_swap_pss` retains `pss_kb + swap_pss_kb`. This keeps CSV ordering
and validation driven by dataclass field order rather than duplicating schema
lists. Keeping the old formula only under the old name would leave the
external framework's expected metric unavailable.

### Treat the CSV schema change as incompatible

Readers will require the new header through `FOOTPRINT_METRICS`; old views are
rejected rather than inferred or translated. The reports have no persisted
compatibility contract, and accepting both shapes would make comparison output
ambiguous.

### Test at derivation and output boundaries

Update profile tests for exact source values and field order, report/view tests
for generated and accepted headers, and comparison tests for the new metric
columns. This confirms every consumer inherits the shared schema.

## Risks / Trade-offs

- [Existing views use the old footprint definition] -> Reject them through
  strict header validation and regenerate views before comparing.
- [The doubled SwapPss formula is non-obvious] -> State the formula and its
  hidumper correspondence in the requirement and test with nonzero SwapPss.

## Migration Plan

1. Regenerate views after deploying the new report format.
2. Use only regenerated, schema-compatible views as comparison inputs.
3. Roll back by restoring the prior producer and regenerating views; existing
   new-format views are intentionally not backward compatible.
