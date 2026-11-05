# Tasks

## 1. Flow And Command Layer

- [x] 1.1 Add strict `Displacement`, refactor `SwipePayload` to inherit it, and add canonical `MultiTouchInputPayload` with a `finger_list` fixed at two or three displacements and inline strict `duration_ms` in `1..15000`; add the command to schema and macro support, and verify malformed counts, coordinates, durations, and extra fields are rejected.
- [x] 1.2 Add the public `lib.multi_touch_input(finger_list: list[dict[str, int]], duration_ms: int)` builder without exposing schema classes; verify it returns the canonical command and propagates strict validation failures.
- [x] 1.3 Add `Device.multi_touch_input(finger_list, duration_ms)` and command execution that converts every multi-touch displacement from cached screen bounds into two or three pixel-coordinate start/end pairs; verify unit tests assert synchronized path delegation.
- [x] 1.4 Add private `lib._validate_command_support(flow, device)` and validate every command in every AppFlow after `lib.run` revalidates the flow and before result-store creation; reject unsupported ABD multi-touch input before output reservation or device action, and verify HDC and ABD support-query behavior.

## 2. Backend Delivery

- [x] 2.1 Implement ABD `is_command_supported` rejection for `multi_touch_input`; verify its backend and runner tests reject the command before device actions.
- [x] 2.2 Implement HDC multi-touch delivery with `uinput -D 0 -T -m`, mapping two through three flow paths and duration into one command; reject nonzero return codes and non-empty stderr while accepting normal stdout; document the stdout-only failure detection security limitation and verify backend unit tests assert command construction and observable failure cases.
- [x] 2.3 Run the bounded HDC two-contact probe using the implemented backend path; verify concurrent device-side pointer evidence and confirm that no touch remains active. Evidence: `hidumper -s 3101 -a -e` showed IDs `10000` and `10001` concurrently down at `(480,1416)` and `(800,1416)`, then both up at `(420,1416)` and `(860,1416)` after the 300 ms gesture.

## 3. Recorder Conversion

- [x] 3.1 Add typed conversion of `uiRecord` pointer rows with two or three complete `fingerList` paths into `multi_touch_input`, before single-pointer gesture-label conversion; normalize every endpoint and convert the raw recorder duration to rounded, clamped milliseconds.
- [x] 3.2 Reject malformed multi-contact data and warn/skip records with more than three paths while retaining existing one-path conversion; verify recorder tests cover two- and three-path swipe, drag, and other `OP_TYPE` conversion, coordinate normalization, duration conversion, malformed fields, skipped over-limit records, and preserved one-path conversion.

## 4. Integration

- [x] 4.1 Add representative editable multi-touch examples to flow documentation or fixtures; verify each example validates through the public flow-loading API.
- [x] 4.2 Run the focused schema, public facade, command, backend, recorder, runner, and full project test suites; verify all pass and document device-only validation limitations.
