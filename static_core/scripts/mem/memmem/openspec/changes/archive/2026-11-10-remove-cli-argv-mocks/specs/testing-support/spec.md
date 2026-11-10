# Spec Delta

## ADDED Requirements

### Requirement: CLI entrypoints accept explicit argument lists
CLI entrypoints SHALL accept an optional argument list and parse process arguments only when it is absent. Tests SHALL invoke entrypoints with explicit arguments rather than altering `sys.argv`.

#### Scenario: Explicit arguments are supplied
- **WHEN** a caller invokes an entrypoint with an argument list
- **THEN** parsing uses that list without reading or modifying process arguments
