# Design

## Context

See `proposal.md` for motivation. `PydanticModel` is the common base for flow and metadata models. It currently forbids unknown fields but leaves Pydantic's default builtin coercion enabled. A small number of fields use `pydantic.Strict*` aliases, which provide runtime strictness but degrade mypy field inference to `Any`.

## Goals / Non-Goals

**Goals:**

- Establish one strict-validation policy for all Pydantic models in the project.
- Give every model field a normal builtin annotation that mypy understands precisely.
- Preserve supported JSON flow and metadata loading behavior.

**Non-Goals:**

- Change model field names, output metadata layout, command semantics, or public function signatures.
- Add dependencies or introduce custom Pydantic validators solely to duplicate built-in strict validation.

## Decisions

### Set strict mode on the shared model base

Configure the shared base model with Pydantic strict mode in addition to its existing extra-field rejection. Configuration is inherited and merged by subclasses that add serialization or validation-alias settings, giving every model one consistent policy.

The alternative of adding field-level strict metadata preserves selective behavior but duplicates policy and leaves new plain builtin fields coercive. Keeping `Strict*` aliases retains the current mypy `Any` problem.

### Replace strict aliases with builtin annotations

Replace every `pydantic.Strict*` annotation with its builtin equivalent. Existing constrained aliases may retain their constraint metadata because it already uses builtin base types. Strict mode supplies runtime rejection uniformly while the builtin annotation provides precise static inference.

### Test Python and JSON validation paths separately

Add focused tests that direct construction/model validation rejects coercible values across affected models, while serialized flow and metadata JSON round trips continue to pass. Pydantic strict JSON validation intentionally accepts JSON representations for supported non-builtin types such as paths; this remains required for existing view metadata round trips.

## Risks / Trade-offs

- [Broader input rejection] → Treat the behavior as breaking, cover representative former coercions, and retain valid JSON round-trip tests.
- [Strict mode affects unannotated future builtin fields] → Make strict-by-default policy explicit in the base model and test it through ordinary builtin annotations.
- [JSON/Python strict-mode differences for specialized types] → Validate both paths and retain the existing serialized metadata contract.

## Migration Plan

1. Enable strict mode in the shared base model and replace strict aliases with builtin annotations.
2. Update tests for rejection and JSON compatibility, then run static checking and the full test suite.
3. Roll back by removing strict mode from the base configuration if a supported external input contract is found to require coercion.
