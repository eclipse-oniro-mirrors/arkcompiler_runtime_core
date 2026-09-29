# Spec Delta

## MODIFIED Requirements

### Requirement: Programmatic API exports public type names
The system SHALL expose through `lib.py` the type names needed to write mypy-checked programmatic user scripts without importing documented APIs from `src.*`. Public type exports SHALL include flow model types, command model types used by public builders, the backend-neutral Device protocol, HDC and ABD handle types, device handle types used by public helper and run signatures, the preprocessing function `preprocess_flow`, the unprocessed flow and app-flow model types `UnprocessedFlow` and `UnprocessedAppFlow`, the macro union type `Macro`, the repeat macro type `RepeatMacro`, and the unprocessed command types `UnprocessedSnapshotCommand` and `UnprocessedScreenshotCommand`. Payload structs, macro payload structs such as `RepeatMacroPayload`, the unprocessed command union `UnprocessedCommand`, and internal runner orchestration types SHALL NOT be public exports.

#### Scenario: Typed user script annotates device handles
- **WHEN** a user script annotates HDC, ABD, or device variables with public `lib.py` type names
- **THEN** mypy can resolve those names from `lib.py` without `src.*` imports

#### Scenario: Existing HDC type remains available
- **WHEN** a documented existing user script imports `lib.Hdc` and `lib.Device`
- **THEN** those public names remain available

#### Scenario: Typed user script annotates flow helpers
- **WHEN** a user script annotates helpers with public flow or command types
- **THEN** mypy resolves those names from `lib.py` without `src.*` imports

#### Scenario: Precise command types are available
- **WHEN** a user script needs precise public builder return annotations
- **THEN** the corresponding command type names are available from `lib.py`

#### Scenario: Typed user script annotates unprocessed types
- **WHEN** a user script annotates public unprocessed flow or macro types
- **THEN** mypy resolves those names from `lib.py`

#### Scenario: Preprocessing function is available from the facade
- **WHEN** a user script calls or annotates `lib.preprocess_flow`
- **THEN** the function is available without `src.*` imports

#### Scenario: Payload structs remain private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** payload struct names are not part of the public facade

#### Scenario: Unprocessed command union remains private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** the unprocessed command union is not part of the public facade

#### Scenario: Internal runner options remain private
- **WHEN** a user imports documented public helpers from `lib.py`
- **THEN** internal orchestration types are not part of the public facade

### Requirement: Programmatic API creates device handles through public helpers
The system SHALL provide public `lib.py` helpers for constructing HDC, ABD, and their matching device handles without requiring documented user scripts to import `src.*` modules. `lib.get_hdc_device()` SHALL construct an HDC device handle, and ABD helpers SHALL be additive.

#### Scenario: HDC helper accepts path-like input
- **WHEN** a caller invokes `lib.get_hdc()` and `lib.get_hdc_device()` with an HDC executable path and HDC handle
- **THEN** the system returns a device handle usable by `lib.run()`

#### Scenario: Device helper wraps HDC handle
- **WHEN** a caller invokes `lib.get_hdc_device()` with an HDC handle
- **THEN** the system returns an HDC device handle usable by `lib.run()`

#### Scenario: ABD helper accepts path-like input
- **WHEN** a caller invokes the ABD helper with a string or `pathlib.Path` ABD executable path
- **THEN** the system returns an ABD handle usable by the public Andr0id device helper

#### Scenario: Andr0id device helper wraps ABD handle
- **WHEN** a caller invokes the Andr0id device helper with an ABD handle returned by the ABD helper
- **THEN** the system returns a device handle usable by `lib.run()`

#### Scenario: Public script avoids src imports
- **WHEN** a user follows documented programmatic API examples
- **THEN** the script imports from `lib.py` and does not need `src.*` imports
