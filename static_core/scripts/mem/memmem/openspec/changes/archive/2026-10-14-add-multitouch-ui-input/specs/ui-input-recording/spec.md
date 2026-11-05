# Spec Delta

## MODIFIED Requirements

### Requirement: Recorder converts supported events to flow commands
The recorder SHALL convert supported recorded events into existing canonical benchmark command actions, SHALL NOT insert `wait` or evidence commands, SHALL preserve the recorded input order inside generated app-flow segments, SHALL preserve generated app-flow segment order, and SHALL validate the generated `Flow` before writing it. The recorder SHALL treat malformed records and unknown event types as fatal conversion errors. The recorder SHALL print warnings immediately for unsupported pointer operations or skipped key codes and SHALL NOT accumulate warnings for later output. A pointer record with two or three complete `fingerList` paths SHALL convert to one canonical `multi_touch_input` command before conversion by `OP_TYPE`: every start and end coordinate SHALL be normalized using the recorded bounds, and the raw recorder duration SHALL be converted to a rounded millisecond value constrained to `1..15000`. The generated command payload SHALL name its contact list `finger_list`.

#### Scenario: Tap-like pointer events convert
- **WHEN** the record contains pointer events with `OP_TYPE` `click`, `doubleClick`, or `longClick`
- **THEN** the generated flow contains corresponding `tap`, `double_tap`, or `long_tap` commands with normalized `x_pct` and `y_pct` payloads

#### Scenario: Swipe-like pointer events convert
- **WHEN** the record contains pointer events with `OP_TYPE` `swipe`, `drag`, or `fling`
- **THEN** the generated flow contains corresponding `swipe`, `drag`, or `fling` commands with normalized start and end coordinates and strict integer velocity values clamped to `200..40000`

#### Scenario: Fling event includes step length
- **WHEN** the record contains a pointer event with `OP_TYPE` `fling`
- **THEN** the generated command payload includes a positive strict integer `step_length`

#### Scenario: Supported key event converts
- **WHEN** the record contains a key event that maps to `Home`, `Back`, or `Power`
- **THEN** the generated flow contains a corresponding `key` command

#### Scenario: Unknown key event is skipped
- **WHEN** the record contains a key event that does not map to `Home`, `Back`, or `Power`
- **THEN** the recorder prints a warning immediately and skips it rather than substituting another key

#### Scenario: Text entry remains UI input
- **WHEN** soft-keyboard text entry appears in the recorder as pointer click or double-click events
- **THEN** the recorder converts those records as pointer commands rather than inferring semantic `text` or `input_text` commands

#### Scenario: Unsupported pointer operation is skipped
- **WHEN** the record contains a pointer event with no equivalent benchmark command
- **THEN** the recorder prints a warning immediately and skips it rather than emitting an invalid command

#### Scenario: Unknown event type is fatal
- **WHEN** the record contains an event whose `EVENT_TYPE` is not supported by the typed recorder-event schema
- **THEN** the recorder exits with an error and does not write a flow file

#### Scenario: Recorded multi-finger input converts
- **WHEN** the record contains a pointer event with any `OP_TYPE`, two or three complete finger paths, and a raw duration
- **THEN** the recorder emits one `multi_touch_input` command containing each normalized path and a duration in the canonical millisecond range

#### Scenario: Recorded multi-touch gesture is malformed
- **WHEN** a multi-contact pointer record is missing a coordinate required by any of its paths or has an invalid raw duration
- **THEN** the recorder fails conversion and does not write a flow file

#### Scenario: Recorded multi-touch contact count is unsupported
- **WHEN** a pointer record has more than three complete `fingerList` paths
- **THEN** the recorder prints an immediate warning and skips that record

#### Scenario: Recorded one-finger gesture keeps existing conversion
- **WHEN** a pointer record has exactly one `fingerList` path
- **THEN** the recorder converts it according to its existing `OP_TYPE` behavior rather than emitting `multi_touch_input`
