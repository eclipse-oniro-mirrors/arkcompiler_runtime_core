# Proposal

## Why

Rendering per-app, per-metric SVGs is expensive for long benchmark scenarios,
even when users only need CSV reports. View creation needs a consistent way to
skip this optional presentation work.

## What Changes

- Add shared `--plots` and `--no-plots` view options, with plotting enabled by
  default, to both benchmark and standalone view CLIs.
- Add a `plots` boolean to root view metadata recording whether rendering was
  enabled; views without it are invalid.
- Keep the required `plots/` directory when rendering is disabled, but omit
  SVG generation.
- Extend the public `create_view` API with a plotting option.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `result-evidence`: Make rendering optional and declare the selected policy in
  view metadata.
- `programmatic-interface`: Expose plotting policy through `create_view`.

## Impact

- Shared CLI arguments, view creation facade, report writer, metadata model,
  view reader, tests, and view-output documentation contracts.
