# log-streaming Specification

## Purpose

Defines run-wide device-local hilog stream lifecycle behavior.

## Requirements

### Requirement: Run-wide hilog stream lifecycle
The system SHALL preserve one asynchronous backend device-log stream per execution when `device_log` is enabled, writing the fixed remote `logs/log.log` and receiving it at evidence `logs/log.log`. The stream SHALL start after remote logs-directory creation and startup validation and execution-context preparation, before AppFlow launch; remain active until finalization; and stop on success or failure after startup. An already exited host stream process SHALL fail execution regardless of its host exit code; finalization SHALL still attempt partial-log receiving and acquired remote cleanup. Stream start and stop SHALL use generic portable child-process APIs, not Unix process groups. HDC SHALL retain continuous hilog behavior and ABD SHALL retain logcat behavior. Logs SHALL not affect local evidence validity or equivalence, although enabled execution stream or receive failures SHALL still fail execution.

#### Scenario: Stream starts
- **WHEN** device logging is enabled and startup validation and remote setup succeeded
- **THEN** the backend starts streaming to `logs/log.log` before launching apps

#### Scenario: Host stream exits early
- **WHEN** the stream process has already exited before requested shutdown, including code zero
- **THEN** execution fails with host status context and still attempts to receive any partial log

#### Scenario: Execution fails after stream startup
- **WHEN** a later phase fails
- **THEN** finalization attempts stream shutdown, all pending receives, and acquired remote cleanup without deleting local partial evidence

#### Scenario: Device logging is disabled
- **WHEN** `device_log` is false
- **THEN** stream configuration, start, stop, tracking, and receive are skipped

#### Scenario: Stream stop is portable
- **WHEN** shutdown is requested
- **THEN** generic process terminate, wait, and kill behavior is used without `os.setsid`, `os.killpg`, or Unix process-group signals

#### Scenario: Streaming starts after remote hilog path exists
- **WHEN** enabled backend logging starts
- **THEN** the replacement remote logs directory already exists and the stream targets `logs/log.log`

#### Scenario: Streaming starts after startup validation actions
- **WHEN** the log stream starts
- **THEN** device health verification and execution context preparation have succeeded and apps have not launched

#### Scenario: Streaming stops after successful AppFlows
- **WHEN** all AppFlows succeed with logging enabled
- **THEN** finalization stops the stream and retains its file for receiving

#### Scenario: Streaming stops after benchmark failure
- **WHEN** execution fails after stream startup
- **THEN** finalization still attempts shutdown and receiving

#### Scenario: Hilog stream exits before requested shutdown
- **WHEN** the host stream process has exited early, even with code zero
- **THEN** execution fails and reports the host status

#### Scenario: Partial hilog is preserved after early exit
- **WHEN** an early exit leaves a partial remote log
- **THEN** finalization attempts receiving it at `logs/log.log`, writes metadata, and cleans acquired remote storage before propagating failure

#### Scenario: Streaming stop uses portable process APIs
- **WHEN** a started stream is stopped
- **THEN** portable generic child-process operations are used without Unix process groups

#### Scenario: Streaming is disabled
- **WHEN** `device_log` is `False`
- **THEN** no log configuration, start, stop, tracking, or receive occurs

### Requirement: HDC hilog stream assumptions are explicit
The system SHALL treat `hilog -x` as a buffered non-blocking dump and SHALL use `hilog` without `-x` for continuous collection.

#### Scenario: Buffered dump is avoided
- **WHEN** hilog collection is enabled
- **THEN** the system does not rely on `hilog -x` for benchmark runtime coverage

#### Scenario: Continuous command is used
- **WHEN** the run-wide hilog stream is started
- **THEN** the system invokes an HDC shell command equivalent to `hilog > <remote_path>` without `-x`
