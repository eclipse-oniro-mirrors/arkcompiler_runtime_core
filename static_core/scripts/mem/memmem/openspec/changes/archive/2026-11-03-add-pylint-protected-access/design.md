# Design

## Context

See [proposal.md](proposal.md) for motivation. The existing Pylint Make target deliberately enables a narrow allowlist of diagnostics. Enabling `protected-access` against the same target set currently reports one test-only violation: the comparison partial-output test saves and calls `compare._write_csv` to complete its first write before inducing a later failure.

## Goals / Non-Goals

**Goals:**

- Extend the existing Pylint allowlist with `protected-access` (`W0212`).
- Remove every reported protected-member access while retaining the intended partial-output assertion.

**Non-Goals:**

- Add public production APIs solely to support tests.
- Expand the Pylint gate beyond the three selected diagnostics.
- Alter comparison output, partial-output retention, or error behavior.

## Decisions

### Reuse the comparison test's local CSV writer

`CompareTest` already has a test-local `_write_csv` helper that serializes rows with the standard-library CSV writer. The failure-injection callback will use that helper for the summary output instead of saving `compare._write_csv`. The comparison code remains private and its test continues to exercise the real public `compare_reports` workflow.

Alternative: promote `compare._write_csv` to a public production function. This is rejected because the helper is an internal implementation detail with no production caller that needs a public contract.

### Extend the existing explicit Pylint allowlist

The Make target will add `protected-access` alongside `too-many-nested-blocks` and `wrong-import-order`, retaining `--disable=all`. This prevents access to private members without enabling unrelated Pylint diagnostics.

Alternative: add a file-local Pylint suppression. This is rejected because the reported access has a straightforward test-local replacement and the requested rule must be enforced rather than bypassed.

## Risks / Trade-offs

- [A future protected access needs a different test seam] -> Prefer existing public behavior or test-local fixtures; add a public API only when it has a real production consumer.
- [The new rule exposes additional findings after Pylint upgrades] -> Keep the existing Pylint version pin and address findings deliberately during upgrades.

## Migration Plan

1. Add the diagnostic to the Make target.
2. Replace the known protected access in the comparison test.
3. Run `make pylint` and the affected test module, then `make tests_full`.
4. Roll back by removing the diagnostic and reverting the test-only callback change; no runtime or persisted data changes are involved.
