# Spec Delta

## MODIFIED Requirements

### Requirement: CLI entrypoints accept explicit argument lists
CLI entrypoints SHALL require an argument list and parse only that list. Executable `__main__` boundaries SHALL pass process arguments excluding the executable name to their entrypoint. Tests SHALL invoke entrypoints with explicit argument lists rather than altering `sys.argv`.

#### Scenario: Explicit arguments are supplied
- **WHEN** a caller invokes an entrypoint with an argument list
- **THEN** parsing uses that list without reading or modifying process arguments

#### Scenario: Entrypoint runs as an executable
- **WHEN** a CLI module is executed as `__main__`
- **THEN** its entrypoint receives `sys.argv[1:]`
