# Spec Delta

## ADDED Requirements

### Requirement: Programmatic API constructs multi-touch commands
The system SHALL provide `lib.multi_touch_input(finger_list: list[dict[str, int]], duration_ms: int)`. The wrapper SHALL construct and return the canonical validated `MultiTouchInputCommand` immediately, SHALL expose no schema class or dataclass in its parameters, and SHALL introduce no hidden defaults.

#### Scenario: Multi-touch wrapper constructs a canonical command
- **WHEN** a caller invokes `lib.multi_touch_input` with a valid two- or three-item finger list and duration
- **THEN** the wrapper returns a validated canonical `MultiTouchInputCommand`

#### Scenario: Multi-touch wrapper validates immediately
- **WHEN** a caller invokes `lib.multi_touch_input` with an invalid finger list or duration
- **THEN** the wrapper raises schema validation failure during construction
