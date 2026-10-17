# Tasks

## 1. Error Aggregation

- [x] 1.1 Add `src.errors.CombinedError` with private original/combined state, contextual combination, retrieval, and exception chaining; verify focused `test/errors_test.py` coverage passes for empty, first, subsequent, and context-free errors.
- [x] 1.2 Replace runner-local error accumulation in benchmark execution, AppFlow cleanup, post-local finalization, child shutdown, and artifact receipt with `CombinedError`; verify existing failure tests preserve primary causes and contextual error messages.

## 2. Lifecycle Ownership

- [x] 2.1 Remove `_RunLifecycle` and precreated-local-root parameters so each runner invocation creates its own output root, while repeated `lib.run()` calls retain top-level atomic reservation; verify existing-output and repeated-run regression tests pass.
- [x] 2.2 Restructure local and remote setup/finalization around successful acquisition boundaries, including immediate cleanup after partial remote setup and remote cleanup after local finalization; verify runner tests cover local setup failure, pre-remote failure, partial remote setup, and cleanup ordering.

## 3. Validation

- [x] 3.1 Run `make tests_full` with the virtual environment tools available on `PATH` and verify formatting, all unit tests, and mypy pass.
