# Design

## Context

See proposal.md for motivation. Schema and metadata models each repeat
`extra="forbid"`, while recorder event models accept evolving device-produced
payloads. The change must consolidate the default without changing strict model
validation or recorder tolerance of unknown input fields.

## Goals / Non-Goals

**Goals:**

- Define the project-wide Pydantic default in one model base.
- Make every project Pydantic model inherit that base directly or transitively.
- Preserve strict schema/metadata models and ignored unknown recorder fields.
- Cover both inherited strictness and explicit recorder exceptions with tests.

**Non-Goals:**

- Changing field types, aliases, validators, serialization behavior, or public
  API signatures.
- Introducing Pydantic dependencies or a broader validation framework.
- Changing how recorder events are converted into benchmark flows.

## Decisions

### Use one flat helper module

Add `src/pydantic_helpers.py` containing a single project base model whose
configuration forbids unknown fields. The existing `src/` package is flat, so a
module avoids introducing a one-file package solely for this base.

Alternatives considered:

- Keep configuration declarations on every model: preserves the current shape
  but leaves future defaults scattered.
- Put the base in `schema.py`: creates an inappropriate dependency from metadata
  and recorder code on flow schema definitions.
- Create `src/pydantic_helpers/`: adds package structure without a second helper.

### Preserve model-specific configuration by inheritance

All schema and metadata model roots inherit the new base. Existing flow models
retain their alias and name-validation options, relying on Pydantic configuration
merging for the inherited strict-extra default. Existing subclasses continue to
inherit through their current parent models.

### Make recorder ingestion permissive explicitly

`FingerEvent` and `UIEvent` inherit the shared base but override `extra` to
`"ignore"`. `UIEvent` retains `validate_by_name=True`. This documents the
intentional exception at both recorder boundaries while allowing the global
default to become stricter in the future without breaking device event input.

## Risks / Trade-offs

- [An overlooked direct `pydantic.BaseModel` subclass bypasses the default] →
  Inventory all project model declarations and add regression tests for strict
  and permissive paths.
- [Pydantic config merging changes a model-specific option] → Preserve only the
  non-default configuration on subclasses and run schema, metadata, recorder,
  and type-checking suites.
- [Future default additions affect recorder parsing] → Recorder models declare
  their permissive extra-field behavior locally.

## Migration Plan

The change is source-only and requires no data migration. If a regression is
found, restore the affected model's local configuration while retaining the
shared base, then rerun the focused model tests and full suite.
