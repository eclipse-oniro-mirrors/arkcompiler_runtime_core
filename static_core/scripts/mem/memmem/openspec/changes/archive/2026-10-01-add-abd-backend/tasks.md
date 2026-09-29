# Tasks

## 1. Establish Neutral Contracts and HDC Layout

- [x] 1.1 Add `CommandResult` and the `Device` protocol with all runner-required methods in `src/device.py`; verify mypy accepts `FakeDevice` as a Device implementation.
- [x] 1.2 Add `src/connection.py` with the `DeviceConnection` protocol for command, argv-shell, raw-shell, timeout, and child-process operations; verify connection protocol tests type-check.
- [x] 1.3 Create `src/platform/ohos/` packages, move the HDC transport to `hdc.py`, and move the concrete OpenHarmony behavior to `hdc_device.py`; verify existing HDC command-translation tests pass with updated imports.
- [x] 1.4 Replace HDC-named results and HDC-specific log operation names in runner, commands, mocks, and tests with neutral contract names; verify the complete unit suite passes for HDC behavior.

## 2. Add ABD Transport and Andr0id Device Behavior

- [x] 2.1 Add `src/platform/andr0id/abd.py` implementing `DeviceConnection` with the same timeout, quoting, raw-shell, and child-process semantics as HDC; verify unit tests cover command argv, metacharacters, timeout, and streaming process construction.
- [x] 2.2 Add `src/platform/andr0id/abd_device.py` implementing app launch/termination, PID, rooted smaps, timestamps, directories, screenshots, push/pull, reboot, readiness, screen timeout, wakeup, logcat capture, screen bounds, and synthetic health; verify exact ABD command-translation tests for each operation.
- [x] 2.3 Implement Andr0id named-key, tap, double-tap, long-tap, swipe, drag, fling, directional-fling, coordinate text, and focused-text translations with best-effort gesture derivation; verify each valid flow action produces an ABD input command without validation failure.
- [x] 2.4 Add focused tests that verify Andr0id force-stop treats a successful ABD invocation as success and Andr0id health returns a runner-accepted synthetic healthy result.

## 3. Select and Construct Backends

- [x] 3.1 Change `Config` and `load_config` to load optional `HDC_PATH` and `ABD_PATH`, normalizing missing or empty values to `None` without backend validation; verify configuration tests cover all missing, empty, and present combinations.
- [x] 3.2 Add `run.py --backend {hdc,abd}` with HDC as the default and construct the matching production device; verify CLI tests cover default HDC, explicit ABD, invalid backend, selected-path absence, and unselected-path absence.
- [x] 3.3 Rename `lib.get_device` to `lib.get_hdc_device`, add ABD construction helpers and public type exports, and verify mypy scripts can construct and annotate both backend device handles without `src.*` imports.

## 4. Restrict Recording and Update Documentation

- [x] 4.1 Rename `record.py` to `record_hdc.py`, validate `HDC_PATH` at that entry point, and update recorder imports and tests; verify the recorder rejects absent HDC configuration before device actions.
- [x] 4.2 Update README prerequisites, `.env` example, run examples, Andr0id rooted-device requirement, package/activity interpretation, approximate input behavior, logcat behavior, and HDC-only recording command; verify documented commands and paths match CLI help and entry point names.

## 5. Validate Integration

- [x] 5.1 Run `make test` and `make mypy`; verify all backend-neutral, HDC, and ABD unit tests pass.
- [x] 5.2 Run `make tests_full`; verify formatting, static checks, and the full test suite pass, or document environment-specific limitations.
