# Spec Delta

## MODIFIED Requirements

### Requirement: Recorder CLI captures UI inputs
The system SHALL provide a root-level `record_hdc.py` CLI that records UI input events from a connected OpenHarmony device using device-side `uitest uiRecord record` as the sole recording source and retrieves `/data/local/tmp/record.csv` after recording stops. The recorder SHALL require a non-empty `HDC_PATH` configuration value and SHALL not select or support the ABD backend.

#### Scenario: Recorder starts successfully
- **WHEN** the user runs `record_hdc.py` with a configured HDC executable and the connected device supports `uitest uiRecord record`
- **THEN** the system starts recording, waits until `Started Recording Successfully...` is observed from raw recorder output without echoing raw HDC output, prints that recording is ready, and prompts the user to perform inputs

#### Scenario: Recorder HDC path is absent
- **WHEN** the user runs `record_hdc.py` without a configured HDC executable path
- **THEN** the system exits before device actions with an error identifying `HDC_PATH`

#### Scenario: Recorder readiness times out
- **WHEN** the recorder process does not report readiness within 120 seconds
- **THEN** the system exits with an error and does not start the user recording timeout

#### Scenario: Recorder fails to start
- **WHEN** the connected device does not support `uitest uiRecord record` or HDC cannot start the command
- **THEN** the system exits with an error and does not write a flow file

#### Scenario: Existing device recording file is replaced
- **WHEN** the recorder starts a new session
- **THEN** the device-side `/data/local/tmp/record.csv` content for that session is used as the source of generated commands

#### Scenario: ABD backend is not offered by the recorder
- **WHEN** the user invokes the recording CLI
- **THEN** it does not accept a backend-selection argument and does not attempt Andr0id UI recording
