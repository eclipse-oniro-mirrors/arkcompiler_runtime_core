# Design

## Context

See `proposal.md` for motivation. Existing flow commands express one pointer at a time. The connected OpenHarmony device provides `uinput -D 0 -T -m`, which accepts one through three synchronized touch paths. Andr0id multi-touch delivery is explicitly out of scope. `uitest uiRecord` emits one `fingerList` entry per tracked contact, but classifies the complete gesture independently of the contact count.

## Goals / Non-Goals

**Goals:**
- Define one backend-neutral two-through-three-contact gesture contract.
- Deliver genuinely concurrent touch paths on HDC devices.
- Reject ABD flows requiring multi-touch input before device actions.
- Convert eligible multi-contact `uiRecord` events to the canonical gesture.

**Non-Goals:**
- Fewer than two or more than three contact paths, Andr0id delivery, rotation, pressure/size replay, or gesture recognition.
- A sequential swipe approximation or changes to existing command semantics.

## Decisions

### Canonical command represents two through three paths
Use `action: "multi_touch_input"` with a `finger_list` of two through three normalized paths and `duration_ms` in `1..15000`. Each `finger_list` entry has a start and end coordinate. A single path duplicates existing swipe and drag functionality, so it is invalid for this command. The hard maximum matches the connected device's `uinput` limit: three contacts are accepted and four are rejected.

### Schema reuses one displacement type
Define a strict `Displacement` schema with `x1_pct`, `y1_pct`, `x2_pct`, and `y2_pct`. Make the existing `SwipePayload` inherit from it and define `MultiTouchInputPayload.finger_list` as a fixed two- or three-item tuple of `Displacement` values. Define the strict `duration_ms` range inline on `MultiTouchInputPayload`; do not introduce a separate duration alias. The public `lib.py` builder accepts `finger_list: list[dict[str, int]]` and `duration_ms: int`, then constructs the canonical schema model so callers do not import schema classes.

### The device boundary receives pixel paths
The command layer converts every contact path through cached `ScreenBounds` and invokes `Device.multi_touch_input(finger_list, duration_ms)`. `finger_list` contains two or three pixel-coordinate start/end pairs, and `duration_ms` is the validated canonical duration. This keeps normalized-flow validation independent of HDC display pixels.

### HDC uses uinput with explicit display zero
The HDC backend will invoke `uinput -D 0 -T -m` with one start/end coordinate quadruplet per contact followed by `duration_ms`. The utility uses display pixel coordinates directly and performs the complete gesture, including release, in one command. The backend fails on a nonzero HDC return code or non-empty standard error, but accepts standard output because `uinput` emits its normal execution summary there.

### Backends declare support per canonical command
Add `Device.is_command_supported(command: Command) -> bool` as the generic backend support query. HDC returns `True` for every current canonical command. ABD returns `False` for `MultiTouchInputCommand` and `True` for every other current canonical command. Private `lib._validate_command_support` checks every command in every AppFlow. `lib.run` calls it immediately after flow revalidation and before result-store creation. Andr0id `input` constructs one-pointer events only; Andr0id `uinput` delivery remains out of scope.

### Recorder uses contact count, not gesture label
The recorder will convert any pointer record with two or three complete `fingerList` paths to `multi_touch_input`, regardless of `OP_TYPE`. This is required because the upstream recorder defines `OP_PINCH` but its pointer-event processing does not select it; simultaneous swipes and drags are classified by the existing single-pointer gesture logic while retaining every contact path. One-path records continue through their existing `OP_TYPE` conversion. The recorder will normalize each multi-contact path's `X_POSI`, `Y_POSI`, `X2_POSI`, and `Y2_POSI` through the recorded screen bounds. The recorder's raw high-resolution `duration` is converted to a canonical millisecond duration, rounded to the nearest millisecond and clamped to `1..15000`. A malformed multi-contact record fails conversion; a record with more than three paths warns and is skipped consistently with other unsupported pointer operations.

## Risks / Trade-offs

- [HDC uinput is unavailable or lacks multi-touch support on another device] -> Preserve the command error and fail the multi-touch command.
- [SECURITY HOLE: HDC can return zero when uinput reports a device-side failure only on standard output] -> Accept standard output to permit normal uinput summaries; rely on strict flow schema validation to prevent malformed host input, and retain return-code/stderr failure detection. This does not detect all device-side failures and requires a future reliable remote-status interface.
- [Recorder duration representation is clock-resolution dependent] -> Convert the raw duration at the recorder boundary and cover representative values with conversion tests.
- [The HDC utility's smooth duration is device-defined] -> Use the requested duration directly within its documented `1..15000` ms range.

## Migration Plan

Existing flow files remain valid without modification. New HDC flows can add `multi_touch_input`. Rollback is removal of those commands from a flow; no persistent data or protocol format changes are introduced.
