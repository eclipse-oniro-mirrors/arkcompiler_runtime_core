# Spec Delta

## ADDED Requirements

### Requirement: Programmatic builders preserve strict builtin types
Public flow and command builders SHALL validate builtin-typed arguments without coercion and SHALL expose their validated fields as the corresponding builtin Python types. They SHALL reject incompatible builtin values immediately while retaining all existing required fields, defaults, and constraints.

#### Scenario: Builder receives a coercible value
- **WHEN** a caller supplies an incompatible but coercible builtin value to a public flow or command builder
- **THEN** the builder raises a validation error during construction

#### Scenario: Builder receives correctly typed values
- **WHEN** a caller supplies builtin values matching the builder's declared field types
- **THEN** the builder returns the existing validated model with those builtin field types
