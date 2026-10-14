# Logging delta

## MODIFIED Requirements

### Requirement: Runner path warnings use memmem logging
Recoverable runner and view-analysis warnings SHALL use the configured logger, respecting level/destination routing and omitting the logger-added prefix in message inputs. A declared smaps file producing no aggregated mappings SHALL warn once per view-creation call with both file path and pattern using `smaps file {path} has no mappings aggregated for pattern {pattern}`. Missing required evidence/metadata, missing plotted timestamps and missing expected PIDs SHALL be errors, not recoverable skips. No missing-iteration-summary warning SHALL remain because views do not consume intermediate summaries. Recorder conversion warnings, prompts and file announcements SHALL retain their separately specified immediate printing behavior; the recorder's logger controls application/backend diagnostics and fatal-operation reporting, not those user-facing conversion messages.

#### Scenario: No mappings aggregate
- **WHEN** a declared file parses to None and level permits warnings
- **THEN** the view call logs the path and pattern once for that file

#### Scenario: Separate views reuse evidence
- **WHEN** individual and combined view calls parse the same no-match file
- **THEN** each call can log its own warning without global deduplication

#### Scenario: Recorder reports a conversion warning
- **WHEN** recorder conversion encounters an event requiring its existing immediate warning under default error-only logging
- **THEN** that warning is still printed as required by the recording contract rather than suppressed by the runner/view logger policy

#### Scenario: Missing iteration summary does not warn
- **WHEN** evidence has no old per-iteration summary.csv
- **THEN** view creation emits no missing-summary warning because intermediate summaries are no longer inputs

#### Scenario: Missing plot metadata fails
- **WHEN** required snapshot metadata or a plotted timestamp is missing
- **THEN** the operation raises instead of logging a recoverable skip warning

### Requirement: Fatal CLI failures use error output
Each CLI SHALL independently own memmem logger configuration and expose --memmem-log-level and --memmem-log-file, reset at its exit boundary, and report caught operation failures with an error-prefixed line containing `memmem error:` and status 1. Argument syntax errors SHALL remain argparse errors. Public execute_flow/create_view SHALL raise exceptions, never convert errors to logged status returns, and never configure/reset a caller-owned logger. Existing info/warn/err routing, configured-file overwrite behavior and empty-log-path rejection SHALL be preserved.

#### Scenario: CLI catches operation failure
- **WHEN** run, execution, view, comparison or recorder CLI catches a fatal operation error
- **THEN** it reports the error and exits unsuccessfully while resetting its own logger

#### Scenario: Programmatic caller owns logger
- **WHEN** execute_flow or create_view succeeds or raises under caller-configured logging
- **THEN** that logger remains configured for the caller

#### Scenario: CLI fatal error is logged
- **WHEN** a CLI catches a fatal operation exception
- **THEN** it emits the error-prefixed memmem error diagnostic and exits with status 1

#### Scenario: Library fatal error is raised
- **WHEN** execute_flow or create_view encounters a fatal failure
- **THEN** the exception reaches the caller without becoming a logged return status
