# Spec Delta

## ADDED Requirements

### Requirement: Flow schema fields reject coercion
The system SHALL reject a flow whose Python or JSON input supplies a value of the wrong builtin type for any flow-schema field, including app metadata, command payloads, macro payloads, and optional description metadata. It SHALL not coerce strings to numeric values, numeric values to strings or booleans, or other incompatible builtin values before validation. Existing field constraints and macro expansion behavior SHALL remain unchanged.

#### Scenario: Coercible flow values are supplied
- **WHEN** a flow input supplies a numeric string for an integer field, an integer for a boolean field, or a non-string value for a string field
- **THEN** flow validation fails before preprocessing or device actions

#### Scenario: Valid JSON flow is supplied
- **WHEN** a flow JSON document supplies JSON values whose types match the flow schema
- **THEN** the system accepts and processes the flow under the existing validation rules
