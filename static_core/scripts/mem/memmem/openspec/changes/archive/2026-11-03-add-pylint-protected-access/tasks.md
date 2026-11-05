# Tasks

## 1. Enforce Protected Access

- [x] 1.1 Add `protected-access` to the existing explicit Pylint allowlist in `Makefile`; verify `make pylint` reports the known `W0212` finding and no unrelated diagnostics.
- [x] 1.2 Replace the comparison partial-output test's access to `compare._write_csv` with its existing test-local CSV writer, preserving the summary-file and later-write-failure assertions; verify `python -m unittest -v test.compare_test` passes.

## 2. Validate the Gate

- [x] 2.1 Run `make pylint` and resolve every `protected-access` finding across the configured target set; verify the command succeeds with all three selected diagnostics enabled.
- [x] 2.2 Run `make tests_full` and `git diff --check`; verify formatting, Pylint, all unit tests, and mypy pass with only intended changes.
