# Tasks

## 1. Add the Narrow Lint Gate

- [x] 1.1 Add a Python 3.10-compatible, pinned Pylint release and its resolved pinned dependencies to `requirements.txt`; verify `make venv` installs the complete requirements file.
- [x] 1.2 Add a `pylint` Make target for every source, test, and root Python script currently passed to `autopep8`; enable only `too-many-nested-blocks` and `wrong-import-order` (`C0411`), set `max-nested-blocks=4`, and suppress Pylint's score output; verify the target reports no unrelated Pylint diagnostics.
- [x] 1.3 Invoke the `pylint` target after `autopep8` and before unit tests in `tests_full`; update the README's individual-check list with `make pylint`; verify the documented command and `make tests_full` run the nesting check.

## 2. Validate Existing Code

- [x] 2.1 Run `make pylint`, refactor every reported nesting-depth or import-order violation without changing behavior, and verify targeted affected tests pass.
- [x] 2.2 Run `make tests_full` and `git diff --check`; verify formatting, both Pylint rules, all unit tests, and mypy pass with only intended changes.
