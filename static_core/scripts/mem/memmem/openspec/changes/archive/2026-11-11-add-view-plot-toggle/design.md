# Design

## Context

Shared view arguments already feed both `create_view.py` and `run.py`, while
the public facade calls the report writer, which unconditionally renders SVGs.

## Goals / Non-Goals

**Goals:** expose one default-enabled plot switch across both CLIs and the
facade, skip rendering when disabled, and persist that choice in strict view
metadata.

**Non-Goals:** omit `plots/`, validate SVG contents, or make plot policy part
of data comparison compatibility.

## Decisions

- Add a mutually exclusive `--plots`/`--no-plots` pair in `add_view_arguments`.
- Propagate one keyword-only `plots: bool = True` through the facade and report
  writer. This preserves existing callers while making the API explicit.
- Always create `plots/`; conditionally call the renderer. Store the selected
  rendering policy, not SVG presence, as `ViewMetadata.plots`.
- Require the new strict metadata field and exclude it from equivalence.

## Risks / Trade-offs

- [Older views lack metadata policy] -> reject and regenerate them.
- [Skipped rendering masks timestamp errors used only by plotting] -> accept
  this because plot creation is explicitly disabled and reports remain valid.
