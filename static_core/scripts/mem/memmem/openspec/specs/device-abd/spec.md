# device-abd Specification

## Purpose

Defines Andr0id benchmark-device execution through ABD while retaining the benchmark flow and evidence model used by memmem.

## Requirements

### Requirement: ABD wrapper executes commands and remote shell operations
The system SHALL provide an ABD wrapper that executes `<ABD_PATH> <args...>` and returns a neutral command result containing return code, stdout, and stderr without imposing success policy. The wrapper SHALL support `timeout=-1` as no timeout and SHALL return a timeout result with return code 127 when a timed command expires. It SHALL provide argv-style remote shell execution that preserves each caller argument as one POSIX-quoted remote argument, raw remote shell execution for deliberate shell syntax, and child-process shell execution for streaming commands.

#### Scenario: Caller runs an ABD command
- **WHEN** a caller invokes the ABD wrapper with command arguments
- **THEN** the wrapper executes the configured ABD executable with those arguments and returns a neutral command result

#### Scenario: ABD shell argument contains metacharacters
- **WHEN** an argv-style remote shell argument contains spaces, quotes, `;`, `&&`, or `$()`
- **THEN** the wrapper preserves the complete value as one remote argument and does not interpret it as additional shell syntax

#### Scenario: Caller runs deliberate raw shell syntax
- **WHEN** a caller invokes raw shell execution with a command containing redirection, a conditional, or a loop
- **THEN** the wrapper passes the pre-serialized command unchanged to the Andr0id remote shell

#### Scenario: ABD command times out
- **WHEN** an ABD command exceeds a non-negative timeout
- **THEN** the wrapper returns a neutral timeout result with return code 127

### Requirement: Andr0id device backend executes benchmark device operations
The system SHALL provide an Andr0id device backend that supports all runner-required device operations through ABD. It SHALL interpret flow `bundle` as an Andr0id package and `ability` as an Andr0id activity, launch the component, and treat a successful ABD invocation of app force-stop as successful termination. The backend SHALL support rooted smaps capture, screenshots, file transfer, PID checks, timestamps, remote directories, reboot readiness, screen bounds, log capture, named keys, and UI input replay.

#### Scenario: Andr0id app is launched
- **WHEN** the runner starts an AppFlow with bundle and ability values through the ABD backend
- **THEN** the backend launches the corresponding Andr0id package/activity component

#### Scenario: Andr0id app is terminated
- **WHEN** the runner requests termination for a bundle through the ABD backend and the ABD force-stop invocation succeeds
- **THEN** the backend reports termination success

#### Scenario: Andr0id smaps is captured
- **WHEN** the snapshot command captures smaps for a valid PID on the rooted Andr0id device
- **THEN** the backend writes `/proc/<pid>/smaps` to the requested device-local artifact path

#### Scenario: Andr0id screenshot is captured
- **WHEN** the screenshot command captures the current Andr0id screen
- **THEN** the backend writes a PNG screenshot to the requested device-local artifact path

#### Scenario: Andr0id device becomes ready after reboot
- **WHEN** the runner reboots an Andr0id device and waits for readiness
- **THEN** the backend waits for ABD device availability and reports boot completion only after the Andr0id boot-complete property is set

#### Scenario: Andr0id screen bounds are requested
- **WHEN** the runner requests screen bounds through the ABD backend
- **THEN** the backend returns parseable pixel bounds derived from the Andr0id display size

#### Scenario: Andr0id log capture is started
- **WHEN** run-wide log capture is enabled through the ABD backend
- **THEN** the backend starts Andr0id logcat streaming to the requested device-local path through a generic child-process handle

#### Scenario: Andr0id named key is sent
- **WHEN** the runner sends Home, Back, or Power through the ABD backend
- **THEN** the backend sends the corresponding Andr0id key event

### Requirement: Andr0id input replay preserves flow compatibility with documented approximation
The ABD backend SHALL accept every existing UI action in a validated flow. Where Andr0id input commands do not distinguish the flow action or expose its velocity and step-length semantics, the backend SHALL derive a best-effort Andr0id gesture and SHALL not reject an otherwise valid flow solely because its action is approximate.

#### Scenario: Andr0id tap-like action is replayed
- **WHEN** a flow requests tap, double tap, or long tap through the ABD backend
- **THEN** the backend performs the corresponding best-effort Andr0id input action at the requested coordinates

#### Scenario: Andr0id swipe-like action is replayed
- **WHEN** a flow requests swipe, drag, fling, or directional fling through the ABD backend
- **THEN** the backend performs a best-effort Andr0id swipe derived from the requested coordinates and velocity

#### Scenario: Andr0id text action is replayed
- **WHEN** a flow requests coordinate text input or focused text through the ABD backend
- **THEN** the backend performs best-effort Andr0id text input without changing flow validation

### Requirement: Andr0id health check is temporarily synthetic
The ABD backend SHALL return a healthy device-health result without reading Andr0id battery or thermal sources until Andr0id health discovery is implemented.

#### Scenario: Andr0id health is requested
- **WHEN** benchmark preparation requests device health through the ABD backend
- **THEN** the backend returns a result accepted by the runner's health policy
