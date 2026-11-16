# Spec Delta

## MODIFIED Requirements

### Requirement: Programmatic API creates independent views
The facade SHALL expose `create_view(dirs: list[Path], dst: Path, *, smaps_filter: Pattern[str] = re.compile(".*"), plots: bool = True) -> Path`. It SHALL reject an empty source list, duplicate normalized absolute source paths, None/bytes/non-pattern filters, nonboolean plotting values, invalid evidence, or incompatible evidence. It SHALL preserve source order, parse declared raw evidence directly, create exactly the new dst using the uniform view contracts, and record the selected plot policy in root metadata. It SHALL return the normalized absolute dst, never access devices or `.env`, never require earlier views, and never overwrite prior destinations or modify evidence files. New partial views SHALL remain on failure. No source/destination ancestry guard or symlink identity resolution SHALL be imposed; callers SHALL choose separate destinations. Library logging configuration SHALL remain caller-owned.

#### Scenario: Caller disables plots
- **WHEN** a caller invokes `create_view` with `plots=False`
- **THEN** it creates a valid view with no SVG plots and metadata `plots` false

#### Scenario: Multiple views use the same evidence
- **WHEN** a caller supplies the same evidence with different patterns and different new destinations
- **THEN** independent views are created without changing evidence or prior views

#### Scenario: Default filter is omitted
- **WHEN** `create_view` is called without smaps_filter
- **THEN** a compiled string pattern `.*` includes every normalized mapping tag

#### Scenario: Empty and duplicate source lists
- **WHEN** dirs is empty or contains lexical aliases of the same normalized absolute path
- **THEN** the call rejects the input rather than creating a zero-source view or double-counting a source

#### Scenario: Offline evidence is supplied
- **WHEN** valid evidence is copied without parent flow provenance and no backend configuration is available
- **THEN** `create_view` analyzes it without opening a device or requiring parent `flow.json`
