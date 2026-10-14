# Spec Delta

## ADDED Requirements

### Requirement: Andr0id backend rejects multi-touch input
The ABD backend SHALL implement `is_command_supported(command)` and return `False` for `multi_touch_input` while returning `True` for every other current canonical flow command. `lib.run` SHALL reject a flow containing a command the backend reports unsupported before reserving output, performing device actions, or launching an AppFlow. It MUST NOT approximate multi-touch input through sequential single-pointer gestures.

#### Scenario: Andr0id flow requires multi-touch input
- **WHEN** a validated flow contains `multi_touch_input` and the selected backend is ABD
- **THEN** `lib.run` rejects the run before any device action, AppFlow launch, or local output reservation
