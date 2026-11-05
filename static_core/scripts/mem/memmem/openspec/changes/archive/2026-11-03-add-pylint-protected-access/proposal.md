# Proposal

## Why

The Pylint gate enforces nesting depth and import order but does not yet prevent tests from depending on private implementation helpers. The current `protected-access` check finds one such access in `test/compare_test.py`.

## What Changes

- Add Pylint's `protected-access` (`W0212`) diagnostic to the existing explicit allowlist.
- Replace every reported protected-member access with behaviorally equivalent public or test-local code.
- Preserve the comparison partial-output failure test without calling `compare._write_csv`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None.

This is a developer-tooling and test-maintenance change with no product behavior or specification requirement changes.

## Impact

- `Makefile` extends the existing Pylint command by one enabled diagnostic.
- `test/compare_test.py` no longer reaches into a private comparison writer.
- Contributors running `make pylint` or `make tests_full` receive protected-access findings.
