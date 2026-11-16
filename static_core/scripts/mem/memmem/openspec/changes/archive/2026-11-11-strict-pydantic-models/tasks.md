# Tasks

## 1. Strict model policy and annotations

- [x] 1.1 Configure `PydanticModel` to reject coercible builtin values by default while retaining unknown-field rejection; verify subclass model configurations preserve strict validation.
- [x] 1.2 Replace every `pydantic.Strict*` field in `src/metadata.py` and `src/schema.py` with its builtin Python type; verify `make mypy` reports precise types without new errors.

## 2. Validation coverage

- [x] 2.1 Add metadata tests covering rejection of representative coercible direct inputs and successful serialized JSON round trips, including view path metadata; verify `test/metadata_test.py` passes.
- [x] 2.2 Add schema and public-builder tests covering strict flow, command, macro, and description inputs while retaining valid JSON flow processing; verify the relevant schema and facade tests pass.

## 3. Integration verification

- [x] 3.1 Run `make pylint`, `make mypy`, and `make test`; verify all checks pass with strict-by-default validation enabled.
