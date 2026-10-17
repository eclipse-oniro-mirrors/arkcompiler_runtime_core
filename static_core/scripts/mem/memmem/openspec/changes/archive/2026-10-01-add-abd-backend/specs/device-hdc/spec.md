# Spec Delta

## MODIFIED Requirements

### Requirement: HDC wrapper executes generic commands
The system SHALL provide an HDC wrapper that executes `<HDC_PATH> <args...>` and returns a neutral command result containing return code, stdout, and stderr without imposing success policy. The wrapper SHALL accept a command timeout where `timeout=-1` means no timeout. If command execution times out, the wrapper SHALL return a neutral result with return code 127, empty stdout, and an error that identifies the timed-out command.

#### Scenario: Caller runs generic HDC command
- **WHEN** a caller invokes `Hdc.run('list', 'targets')`
- **THEN** the wrapper executes `<HDC_PATH> list targets` and returns a neutral command result

#### Scenario: HDC command exits non-zero
- **WHEN** an HDC command returns a non-zero exit code
- **THEN** the wrapper returns the non-zero return code, stdout, and stderr to the caller

#### Scenario: HDC command times out
- **WHEN** an HDC command exceeds the non-negative timeout provided by the caller
- **THEN** the wrapper returns a neutral timeout result with return code 127

#### Scenario: HDC timeout is disabled
- **WHEN** a caller invokes `Hdc.run` with `timeout=-1`
- **THEN** the wrapper executes the command without a subprocess timeout

### Requirement: Device layer supports benchmark environment controls
The system SHALL provide device operations for waking the device, disabling screen timeout, configuring backend-neutral run-wide device log capture, and starting the log stream to a device-local file through a generic child process handle. For OpenHarmony, the operations SHALL configure and stream hilog. The child process handle MUST allow the caller to stop the host-side stream process using Python built-in cross-platform process APIs.

#### Scenario: Device is woken
- **WHEN** the runner requests device wakeup
- **THEN** the device layer executes `power-shell wakeup` through HDC shell and reports failure to the caller when the command fails

#### Scenario: Screen timeout is disabled
- **WHEN** the runner requests screen timeout disabling
- **THEN** the device layer executes `power-shell timeout -o 60000000` through HDC shell and reports failure to the caller when the command fails

#### Scenario: Hilog is configured
- **WHEN** the runner configures device log capture through the HDC backend
- **THEN** the backend executes `hilog -Q pidoff` and `hilog -p off` and reports failure when either command fails

#### Scenario: Hilog stream is started
- **WHEN** the runner requests log streaming to a remote path through the HDC backend
- **THEN** the backend starts `hilog > <remote_path>` and returns a generic child process handle without streaming logs into host memory

#### Scenario: Hilog stream is stopped
- **WHEN** the caller stops a generic log-stream child process handle
- **THEN** the system terminates the direct host-side stream process, waits for bounded shutdown, and kills it if it remains alive using Python built-in cross-platform process APIs

#### Scenario: Hilog stream path is required
- **WHEN** the runner requests log streaming
- **THEN** the caller MUST provide a device-local remote path and the device layer does not stream logs into host memory
