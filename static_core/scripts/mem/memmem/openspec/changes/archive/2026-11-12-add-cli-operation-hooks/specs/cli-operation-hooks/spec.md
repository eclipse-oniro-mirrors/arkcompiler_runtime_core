# Spec Delta

## Purpose

Defines executable CLI hooks around evidence collection and view creation, allowing users to insert custom actions while library operations remain independent of hooks.

## ADDED Requirements

### Requirement: CLI hook registration is phase-specific and ordered
Execution CLIs SHALL accept repeatable `--pre-execute-flow-hook PATH` and `--post-execute-flow-hook PATH`; view CLIs SHALL accept repeatable `--pre-create-view-hook PATH` and `--post-create-view-hook PATH`. `run.py` SHALL expose all four, `execute_flow.py` only the execution pair, and `create_view.py` only the view pair. Each phase SHALL execute registrations sequentially in argument order, including duplicates. Hook paths SHALL be lexically normalized absolute paths relative to invocation cwd without symlink resolution. Omitted hook options SHALL mean empty hook lists.

#### Scenario: Hooks are interleaved on the command line
- **WHEN** pre-execution A, post-execution B, and pre-execution C are registered in that order
- **THEN** each collection runs A, C, the library operation, then B

#### Scenario: Duplicate executable is registered
- **WHEN** the same hook path appears twice in a phase
- **THEN** it is invoked twice without deduplication or duplicate rejection

#### Scenario: Irrelevant options are supplied
- **WHEN** execution-only CLI receives a view hook or view-only CLI receives an execution hook
- **THEN** argument parsing rejects the option

### Requirement: Hooks surround each atomic CLI operation
CLI orchestration SHALL invoke pre hooks immediately before each corresponding library operation and post hooks only after its successful return. Public library methods SHALL retain their signatures and SHALL neither execute nor record hooks. All collections SHALL precede all views in `run.py`; hooks SHALL surround every individual and combined view without a view-kind discriminator. Pre-execution hooks SHALL precede any reboot performed inside execution.

#### Scenario: Two repeats are requested
- **WHEN** `run.py` collects two repeats successfully
- **THEN** execution hooks surround each of the two collections, followed by view hooks around each individual view and the combined view

#### Scenario: Library operation raises
- **WHEN** evidence collection or view creation raises
- **THEN** its post hooks and all later operations are skipped and existing partial output remains

#### Scenario: Direct library caller operates
- **WHEN** a caller invokes a library operation directly
- **THEN** it performs that atomic operation without discovering or invoking user hooks

### Requirement: Executable hooks receive bounded synchronous invocation
Each hook SHALL be invoked directly as an executable with exactly one argument: an absolute path to its UTF-8 JSON context file. Its cwd SHALL be CLI invocation cwd; stdout and stderr SHALL be inherited without capture. Memmem SHALL not invoke a shell or infer an interpreter from an extension. Each hook SHALL have a 300-second timeout. Nonzero exit, timeout, inability to launch, or another process-runner exception SHALL abort immediately with an informative exception identifying event, executable, and failure reason (return code for nonzero exit or duration for timeout). Later hooks and operations SHALL be skipped. Memmem SHALL emit info-level start and successful-end messages identifying each invocation, including its event and executable; failures SHALL use exceptions rather than dedicated hook-failure log messages. Hook diagnostics SHALL omit the operation destination. These info messages SHALL obey normal logger configuration.

#### Scenario: Hook fails after an earlier successful hook
- **WHEN** the second pre hook exits nonzero
- **THEN** the CLI fails, preserves earlier external effects, skips the library call, and reports the executable and return code

#### Scenario: Post hook fails
- **WHEN** a post hook fails after successful output generation
- **THEN** generated output remains, later hooks and operations stop, and the CLI reports failure

#### Scenario: Multiple long hooks succeed
- **WHEN** two hooks each complete in four minutes
- **THEN** both succeed because the timeout is per executable invocation rather than per phase

#### Scenario: Hook times out or cannot launch
- **WHEN** a hook exceeds 300 seconds or is missing or non-executable
- **THEN** the CLI fails with the event, path, and cause in the exception message

#### Scenario: Logging and child output are independent
- **WHEN** memmem info logging is disabled or redirected to a log file
- **THEN** hook lifecycle messages follow that configuration while child stdout/stderr remain inherited

### Requirement: Hook context describes operation arguments without modifying them
Execution contexts SHALL contain exactly `event`, `flow`, `dst`, `reboot`, and `device_log`, with event `pre_execute_flow` or `post_execute_flow`, canonical flow JSON object, normalized absolute destination string, and boolean options. View contexts SHALL contain exactly `event`, `dirs`, `dst`, `smaps_filter_pattern`, `smaps_filter_flags`, and `plots`, with event `pre_create_view` or `post_create_view`, ordered normalized absolute source strings, normalized absolute destination string, string pattern, integer effective flags, and boolean `plots`. No device handle, backend, repeats, or view-kind field SHALL be included. Memmem SHALL normalize paths before invoking hooks and SHALL not read context edits back into its runtime arguments. Hooks SHALL treat context files as read-only. A phase's temporary context file SHALL be available to all its hooks and deleted after that phase on success or failure; an empty phase SHALL need no context file.

#### Scenario: Python or Bash hook reads context
- **WHEN** an executable opens the JSON path supplied as its first argument
- **THEN** it sees the appropriate schema with the same normalized inputs and options as the associated library call

#### Scenario: Context is modified externally
- **WHEN** a hook edits the context document
- **THEN** memmem does not adopt that edit as a change to planned library arguments

#### Scenario: Phase finishes or fails
- **WHEN** a hook phase completes or aborts
- **THEN** its temporary context is cleaned up and a later post phase, if reached, receives the post event in a new context
