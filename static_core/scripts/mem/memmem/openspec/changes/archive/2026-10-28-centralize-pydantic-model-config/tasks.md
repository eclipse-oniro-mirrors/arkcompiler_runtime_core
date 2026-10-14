# Tasks

## 1. Shared Model Configuration

- [x] 1.1 Add `src/pydantic_helpers.py` with the project Pydantic base model and verify it defines only the inherited `extra="forbid"` default.
- [x] 1.2 Migrate schema and metadata model roots to the shared base, remove redundant strict-extra declarations, and verify schema and metadata tests preserve alias, validation, and unknown-field rejection behavior.

## 2. Recorder Model Exceptions

- [x] 2.1 Migrate `FingerEvent` and `UIEvent` to the shared base while explicitly retaining `extra="ignore"` and UIEvent name validation; verify recorder parsing accepts unknown top-level and nested event fields.
- [x] 2.2 Add or update focused tests that distinguish inherited strict schema/metadata models from permissive recorder ingestion models.

## 3. Integration Verification

- [x] 3.1 Run `make test`, `make mypy`, and `git diff --check` to verify all model migrations and project typing checks pass.
