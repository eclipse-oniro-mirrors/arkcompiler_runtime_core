# Spec Delta

## MODIFIED Requirements

### Requirement: Standalone evidence declares expected artifact files
An execution SHALL exclusively create its exact evidence destination before device actions. It SHALL write root `flow.json` containing the canonical validated flow being executed, root `metadata.json` containing exactly `flow` equal to `"flow.json"`, strict boolean `reboot`, and strict boolean `device_log`, and `apps.json` with an `apps` array of PID, label, bundle, and ability records in launch order; eager `snapshots/metadata.json` and `screenshots/metadata.json`; raw snapshots at `snapshots/<snapshot_label>/<app_label>.smaps`; screenshots at `screenshots/<screenshot_label>.png`; and optional device logs at `logs/log.log`. Canonical flow serialization SHALL retain description metadata and expanded macros. Provenance SHALL be written during local initialization before device preparation, including for direct library calls, and SHALL record configuration rather than completion. It SHALL NOT record hooks, repeats, or backend and SHALL NOT write reports inside evidence. Each artifact metadata file SHALL have an ordered `artifacts` array; each artifact SHALL contain a valid label, a nonnegative integer nanosecond timestamp serialized as a decimal string, and a required nonempty ordered `files` array of relative forward-slash paths. Empty artifact arrays SHALL be accepted. Metadata SHALL declare expected files before capture rather than only received files. Source app and artifact labels SHALL retain existing filename-safe validation. Different app labels resolving to the same PID SHALL remain distinct.

#### Scenario: Flow has no snapshot or screenshot commands
- **WHEN** execution succeeds without either artifact type
- **THEN** both category directories and metadata files exist with empty artifact arrays

#### Scenario: Snapshot captures multiple eligible apps
- **WHEN** a snapshot event starts with eligible labels App and Other
- **THEN** its ordered expected files are `<label>/App.smaps` and `<label>/Other.smaps` before capture, and one event timestamp applies to both

#### Scenario: Screenshot capture fails
- **WHEN** capture of a declared screenshot fails
- **THEN** execution fails and its expected PNG declaration remains in partial evidence rather than being removed

#### Scenario: Logs are disabled
- **WHEN** execution uses device_log false
- **THEN** no device-log setup, stream or receive is performed and no log directory is required

#### Scenario: Direct library execution initializes evidence
- **WHEN** local evidence initialization succeeds for a direct library call
- **THEN** root flow and metadata contain the canonical flow and actual reboot/device_log settings without CLI-only information

#### Scenario: Execution fails after local initialization
- **WHEN** device preparation or collection fails after provenance was written
- **THEN** the provenance remains with partial evidence and does not assert successful completion

### Requirement: Evidence validity and equivalence are separate contracts
Evidence readers SHALL require root `metadata.json` with exactly `flow: "flow.json"`, strict boolean `reboot`, and strict boolean `device_log`, and readable root `flow.json` that validates as canonical flow. They SHALL retain both parsed provenance objects in parsed evidence. They SHALL require `apps.json`, both eager artifact directories and their metadata files, valid unique app/category-event identities, required nonempty ordered file arrays, correct relative path structures/extensions, and all declared files as readable files. Snapshot paths SHALL be exactly `<artifact.label>/<known-app-label>.smaps`; screenshot paths SHALL be exactly `<artifact.label>.png`. Absolute/traversing paths and duplicate file identities SHALL be rejected. Undeclared extra files and all log contents/availability SHALL be ignored. Evidence equivalence SHALL compare app label-to-bundle/ability mappings ignoring PIDs and app-array ordering, then ordered snapshot labels and ordered file arrays ignoring timestamps. Canonical flow, execution metadata, screenshots, and logs SHALL NOT affect equivalence. Invalid sources SHALL fail rather than be silently skipped. Legacy structures, including evidence missing the new provenance files, SHALL be rejected. Documentation and code contracts SHALL explicitly state that structural validity does not prove whole-flow completion or file-content integrity; those remain caller responsibility/out of scope. Readers SHALL NOT add flow-to-artifact execution-completeness verification.

#### Scenario: Declared snapshot is absent
- **WHEN** snapshot metadata declares a file that is missing
- **THEN** the reader fails with the affected path rather than treating it as an absent measurement

#### Scenario: Extra files are present
- **WHEN** otherwise valid evidence contains undeclared files
- **THEN** analysis ignores them and uses only declared files

#### Scenario: Metadata differs only in PID and timestamps
- **WHEN** app identities and ordered snapshot declarations match but PID or timestamps differ
- **THEN** the sources are equivalent

#### Scenario: Ordered file arrays differ
- **WHEN** two sources declare the same file members in a different order
- **THEN** they are not equivalent

#### Scenario: Non-memory evidence differs
- **WHEN** two individually valid sources have equivalent memory declarations but different screenshots or log settings
- **THEN** they remain equivalent

#### Scenario: Required provenance is invalid
- **WHEN** root metadata or flow is missing, malformed, has invalid field types, or metadata refers to another flow filename
- **THEN** reading evidence fails before analysis

#### Scenario: Provenance differs
- **WHEN** individually valid sources have equivalent memory declarations but different canonical flows or execution options
- **THEN** their parsed provenance remains accessible and they remain equivalent
