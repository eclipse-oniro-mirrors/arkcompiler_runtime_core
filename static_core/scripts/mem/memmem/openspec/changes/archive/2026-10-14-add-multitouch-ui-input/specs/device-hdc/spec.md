# Spec Delta

## ADDED Requirements

### Requirement: OpenHarmony backend delivers multi-touch input
The HDC backend SHALL use `uinput -D 0 -T -m` when a flow requires `multi_touch_input`. It SHALL deliver two through three contact paths as one synchronized gesture using display pixel coordinates and the requested duration. The backend MUST report a clear delivery failure when the command exits nonzero or writes to standard error. Standard output SHALL be accepted because `uinput` writes normal execution summaries there.

#### Scenario: OpenHarmony multi-touch input executes
- **WHEN** a flow requires `multi_touch_input` with two through three contacts
- **THEN** the backend invokes `uinput -D 0 -T -m` with every contact's display-pixel start and end coordinates and the requested duration

#### Scenario: OpenHarmony multi-touch command reports an observable error
- **WHEN** `uinput` exits nonzero or emits content on standard error
- **THEN** the backend reports multi-touch delivery failure

#### Scenario: OpenHarmony multi-touch command emits normal output
- **WHEN** `uinput` exits zero and emits its normal execution summary on standard output
- **THEN** the backend accepts the delivery result

#### Scenario: Standard-output-only failure is not observable
- **WHEN** `uinput` reports a device-side failure only on standard output while HDC exits zero and standard error is empty
- **THEN** the backend cannot distinguish that failure from normal output; strict schema validation remains the temporary mitigation until a reliable remote-status interface is available

### Requirement: OpenHarmony backend reports canonical command support
The HDC backend SHALL implement `is_command_supported(command)` and return `True` for every current canonical flow command.

#### Scenario: OpenHarmony command support is queried
- **WHEN** `lib._validate_command_support` queries the HDC backend about a canonical flow command
- **THEN** the backend reports that command as supported
