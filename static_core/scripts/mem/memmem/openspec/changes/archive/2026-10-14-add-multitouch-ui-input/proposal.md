# Proposal

## Why

Memmem flows can replay only single-pointer UI inputs. The connected Andr0id and OpenHarmony devices expose multitouch-capable input hardware, but their public `input` and `uitest uiInput` CLIs do not provide a pinch command, preventing reproducible zoom testing.

## What Changes

- Add a canonical `multi_touch_input` flow command with two to three normalized contact paths and a duration.
- Add the matching public `lib.multi_touch_input` builder and reuse the path schema for swipe payloads.
- Add a backend-neutral multi-touch device operation, generic per-command backend support query, and command execution path.
- Deliver the command through `hdc shell uinput -D 0 -T -m`.
- Reject a flow containing the command before device actions when the selected backend is ABD.
- Convert recorder events with two or three recorded contact paths into the canonical command, independent of the recorder's gesture label.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `benchmark-flow`: add a validated multi-touch command and its execution semantics.
- `ui-input-recording`: convert supported recorded multi-touch gestures into the canonical command.
- `device-abd`: reject multi-touch input before benchmark execution.
- `device-hdc`: add true OpenHarmony multi-touch replay beyond `uitest uiInput`.

## Impact

- Affects `src/schema.py`, `src/commands.py`, `src/device.py`, `src/runner.py`, `lib.py`, both production device backends, `record_hdc.py`, and their tests.
- Uses existing device-side input tools and requires no new host dependency.
- Introduces a new validated flow action but does not change existing action semantics.
