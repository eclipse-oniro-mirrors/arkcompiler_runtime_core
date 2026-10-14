# Design

## Context

See [proposal.md](proposal.md) for motivation. `tests_full` currently runs an in-place formatter, unittest discovery, and mypy. The project pins development dependencies in `requirements.txt` and has no existing linter configuration file.

## Goals / Non-Goals

**Goals:**

- Enforce a maximum of four nested blocks across the same production, test, and CLI Python targets formatted today.
- Run exactly two explicitly selected Pylint diagnostics as part of `tests_full`.
- Keep the check reproducible through pinned dependencies.

**Non-Goals:**

- Enforce general Pylint style, naming, complexity, documentation, or type diagnostics beyond import ordering.
- Change formatter, unittest, or mypy behavior.
- Introduce source-level Pylint suppressions or refactor code outside violations discovered by the new check.

## Decisions

### Use an explicit two-rule Pylint allowlist

The Make target will invoke Pylint with all diagnostics disabled, enable only `too-many-nested-blocks` and `wrong-import-order` (`C0411`), and set `max-nested-blocks=4`. This directly enforces the agreed control-flow and import-order limits without inheriting Pylint's broad default policy.

Alternative: enable Pylint defaults and selectively disable unwanted diagnostics. This is rejected because upgrades could expose unrelated rules and broaden the gate unexpectedly.

### Lint every current formatter target

The target will use the same source set as `autopep8`: `src/`, `test/`, and the listed root CLI/library scripts. Applying the rule consistently prevents test and script code from becoming exempt from the maintainability constraint.

Alternative: limit linting to `src/`. This is rejected because the nesting concern has appeared in tests and would leave existing formatter targets inconsistent.

### Pin Pylint and its resolved dependencies

`requirements.txt` will receive Pylint and the dependency versions required by the selected Python 3.10-compatible release. This follows the repository's fully pinned dependency convention and prevents lint-rule behavior from drifting between environments.

Alternative: use an unpinned tool or a system installation. This is rejected because `make venv` must produce a repeatable validation environment.

### Run lint after formatting and before tests

`tests_full` will retain formatting first, then run the nesting check before unit tests and mypy. Formatting still normalizes source before static validation, and a lint failure avoids spending time on later checks.

## Risks / Trade-offs

- [Pylint version changes alter diagnostic interpretation] -> Pin the tool and transitive dependencies; update deliberately.
- [The Make target and formatter target sets drift] -> Keep their target lists visibly aligned in the Makefile and verify coverage during implementation.
- [Existing code exceeds the new limit] -> Run the target before finalizing; refactor only reported violations while preserving behavior.

## Migration Plan

1. Install the pinned dependency set through the existing virtual-environment workflow.
2. Add and run the dedicated Make target.
3. Add it to `tests_full` and resolve reported nesting-depth and import-order violations.
4. Roll back by removing the Make target and dependency additions; no runtime or persisted data changes are involved.
