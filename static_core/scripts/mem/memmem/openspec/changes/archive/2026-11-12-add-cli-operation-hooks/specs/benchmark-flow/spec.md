# Spec Delta

## MODIFIED Requirements

### Requirement: Backend command support is validated before execution
The public `execute_flow` entry SHALL check every canonical command immediately after flow revalidation and before evidence-store creation or reservation or device preparation. It SHALL report the first unsupported command. The shared CLI collection helper SHALL delegate command support validation to `execute_flow` and SHALL NOT duplicate that validation.

#### Scenario: Device rejects an unsupported command
- **WHEN** a validated flow contains a command that the selected device reports unsupported
- **THEN** `execute_flow` fails before evidence creation or device actions

### Requirement: Repeated runs produce iteration output directories
Execution CLIs SHALL create a missing parent `--out-dir` or accept a pre-created parent, then use exact new `iteration_i/evidence` API destinations for `i` from `0` to `repeats - 1`, including `repeats=1`. They SHALL NOT write run-root `flow.json` or run metadata; each library execution SHALL instead persist its canonical flow and execution metadata inside its evidence destination. An intended evidence destination that already exists SHALL cause an error rather than implicit resume. An unrelated parent `flow.json` SHALL not be overwritten or used as a run marker. Reboot SHALL apply before each requested execution. `run.py` SHALL collect all iterations before any view, then create individual `iteration_i/view/view-T` views and one combined `view/view-T`, reusing one invocation timestamp `T` even for a single repeat. `execute_flow.py` SHALL create no views. Collection or hook failure SHALL stop subsequent iterations and all later rendering; rendering failure SHALL stop subsequent views and leave all existing or partial outputs.

#### Scenario: Repeats equals one
- **WHEN** `run.py` uses repeats `1`
- **THEN** it still creates `iteration_0/evidence` and both individual and combined views

#### Scenario: Repeats is greater than one
- **WHEN** repeats exceeds `1`
- **THEN** evidence is collected into `iteration_0` through `iteration_{repeats-1}` before `run.py` starts any view

#### Scenario: Existing repeated-run output is rejected
- **WHEN** an intended evidence destination already exists in the chosen parent
- **THEN** collection rejects the conflict without overwriting it; existence of the parent or an unrelated parent flow file alone is not an error

#### Scenario: Iteration failure aborts the run
- **WHEN** an iteration fails
- **THEN** later iterations and all rendering are skipped while partial evidence is retained

#### Scenario: Reboot applies per iteration
- **WHEN** reboot is enabled
- **THEN** it is requested separately for each execution

#### Scenario: Parent exists without conflicting output
- **WHEN** `--out-dir` is pre-created and evidence targets are available
- **THEN** the CLI accepts the parent and each API exclusively creates its own subdirectory

#### Scenario: Rendering fails
- **WHEN** a per-iteration view fails after collection completed
- **THEN** later individual and combined views are not attempted and evidence and partial views remain

#### Scenario: Each iteration carries canonical provenance
- **WHEN** repeated collection initializes its evidence destinations
- **THEN** each contains canonical `flow.json` and execution `metadata.json`, and the CLI creates neither file at the run root
