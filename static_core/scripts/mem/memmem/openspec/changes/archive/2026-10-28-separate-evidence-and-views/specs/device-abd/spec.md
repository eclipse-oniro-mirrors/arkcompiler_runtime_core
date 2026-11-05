# ABD interface delta

## MODIFIED Requirements

### Requirement: Andr0id backend rejects multi-touch input
The ABD backend SHALL preserve is_command_supported behavior: False for multi_touch_input and True for every other current canonical flow command. The public execute_flow entry SHALL reject unsupported commands before reserving evidence or performing device actions/AppFlow launches. It SHALL NOT approximate multi-touch through sequential single-pointer gestures. Moving execution/view ownership SHALL not change ABD UI command translation or device behavior.

#### Scenario: Andr0id flow requires multi-touch input
- **WHEN** a canonical flow contains multi_touch_input and ABD is selected
- **THEN** execute_flow raises before evidence reservation, device actions or AppFlow launch
