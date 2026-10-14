# Proposal

## Why

The current `tests_full` target formats code and checks behavior and types, but it cannot prevent deeply nested control flow. Recent refactors required manual review to keep nesting at four blocks or fewer.

## What Changes

- Add Pylint as a pinned development dependency.
- Add a Make target that runs Pylint over every target currently formatted by `autopep8`.
- Configure that target to enable only Pylint's `too-many-nested-blocks` and `wrong-import-order` checks, reject nesting deeper than four blocks, and enforce import grouping and ordering.
- Run the new check from `tests_full` without enabling unrelated Pylint diagnostics.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None.

This is a development-tooling-only change and does not modify product behavior or an existing specification requirement.

## Impact

- `requirements.txt` gains Pylint and its pinned dependency set.
- `Makefile` gains the narrow lint target and invokes it from `tests_full`.
- Contributors running `make tests_full` receive a failure for nesting deeper than four blocks or incorrectly ordered imports in production code, tests, and CLI scripts.
