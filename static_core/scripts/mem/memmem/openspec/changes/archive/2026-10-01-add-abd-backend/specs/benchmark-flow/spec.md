# Spec Delta

## MODIFIED Requirements

### Requirement: CLI accepts benchmark flow input
The system SHALL provide a single user-facing CLI through `run.py` that accepts a required `--flow` argument, a `--backend` argument with values `hdc` and `abd`, an optional `--out-dir` argument, an optional `--repeats` argument, reboot controls `--reboot` and `--no-reboot`, hilog controls `--hilog` and `--no-hilog`, and memmem application logging controls `--memmem-log-level` and `--memmem-log-file`. The backend SHALL default to `hdc`. Reboot SHALL default to disabled. Hilog collection SHALL default to enabled and SHALL mean one run-wide device log artifact stored at the existing hilog evidence path. The CLI SHALL read optional HDC and ABD executable paths from `.env` and fail before benchmark execution only when the executable path for the selected backend is missing. The CLI SHALL act as an adapter that reads the JSON flow file, validates it into the flow schema, constructs the selected production device, and delegates execution to the public programmatic API.

#### Scenario: HDC backend is selected by default
- **WHEN** the user runs `python run.py --flow flow.json` without `--backend`
- **THEN** the system selects the HDC backend and requires a configured HDC executable path

#### Scenario: User requests help
- **WHEN** the user runs `python run.py --help`
- **THEN** the system displays command-line help describing flow, backend, output, repeats, reboot, hilog, and memmem logging controls

#### Scenario: User omits flow argument
- **WHEN** the user runs `python run.py` without `--flow`
- **THEN** the system exits with an argument validation error

#### Scenario: User provides flow input
- **WHEN** the user runs `python run.py --flow flow.json`
- **THEN** the system uses default output, reboot, log collection, repeat, and memmem logging options

#### Scenario: User provides out dir explicitly
- **WHEN** the user runs with `--out-dir results/bench1`
- **THEN** the system writes all run output under that directory

#### Scenario: User provides an existing out dir
- **WHEN** the requested output directory already exists
- **THEN** the run fails before device actions with an error naming the directory

#### Scenario: User provides repeats explicitly
- **WHEN** the user runs with `--repeats 3`
- **THEN** benchmark options set repeats to 3 and create iteration output directories

#### Scenario: User provides a non-positive repeats value
- **WHEN** the user provides a non-positive repeats value
- **THEN** the CLI exits with an argument validation error

#### Scenario: User requests reboot explicitly
- **WHEN** the user runs with `--reboot`
- **THEN** benchmark options enable reboot before each iteration

#### Scenario: User disables reboot explicitly
- **WHEN** the user runs with `--no-reboot`
- **THEN** benchmark options disable reboot

#### Scenario: User enables hilog explicitly
- **WHEN** the user runs with `--hilog`
- **THEN** benchmark options enable run-wide device log collection

#### Scenario: User disables hilog explicitly
- **WHEN** the user runs with `--no-hilog`
- **THEN** benchmark options disable log setup, stream lifecycle, artifact tracking, and receive behavior

#### Scenario: User configures memmem log level
- **WHEN** the user runs with `--memmem-log-level info`
- **THEN** benchmark options set memmem application log level to `info`

#### Scenario: User configures memmem log file
- **WHEN** the user runs with `--memmem-log-file memmem.log`
- **THEN** emitted memmem logs are routed to that path

#### Scenario: User provides invalid memmem log level
- **WHEN** the user provides an unsupported memmem log level
- **THEN** the CLI exits with an argument validation error

#### Scenario: User provides removed log controls
- **WHEN** the user provides `--logs` or `--no-logs`
- **THEN** the CLI exits with an argument validation error

#### Scenario: User provides contradictory boolean controls
- **WHEN** the user provides both values of a reboot or hilog boolean pair
- **THEN** the CLI exits with an argument validation error

#### Scenario: CLI delegates to public API
- **WHEN** CLI parsing, config loading, device construction, and flow validation succeed
- **THEN** the CLI calls the public programmatic run API with the validated flow, device, output directory, and runtime options

#### Scenario: ABD backend is selected
- **WHEN** the user runs `python run.py --flow flow.json --backend abd`
- **THEN** the system selects the ABD backend and requires a configured ABD executable path

#### Scenario: Unselected backend path is absent
- **WHEN** the selected backend has a configured executable path and the other backend path is missing or empty
- **THEN** the system starts benchmark preparation without rejecting the unselected backend path

#### Scenario: Selected backend path is absent
- **WHEN** the selected backend has a missing or empty executable path
- **THEN** the system fails before device actions with an error identifying the required configuration value

#### Scenario: Invalid backend is provided
- **WHEN** the user passes a `--backend` value other than `hdc` or `abd`
- **THEN** the CLI exits with an argument validation error

### Requirement: CLI path arguments resolve against the invocation working directory
The system SHALL resolve the `--flow` path, the `--out-dir` path, the `--memmem-log-file` path, and configured non-empty `HDC_PATH` and `ABD_PATH` values against the working directory of the CLI invocation before any later working directory change.

#### Scenario: Relative path arguments are invariant to the run working directory
- **WHEN** the user runs `python run.py --flow flow.json --out-dir out` and benchmark execution later changes the process working directory
- **THEN** the flow file, output directory, memmem log file, and selected backend executable path still resolve to the paths given at invocation time
