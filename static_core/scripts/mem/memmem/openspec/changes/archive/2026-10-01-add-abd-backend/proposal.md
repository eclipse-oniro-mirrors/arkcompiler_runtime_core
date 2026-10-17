# Proposal

## Why

memmem currently communicates only with OpenHarmony devices through HDC. Andr0id benchmark devices need an ABD backend while retaining the same benchmark flow, evidence layout, and runner behavior.

## What Changes

- Add a selectable ABD backend to `run.py` through `--backend {hdc,abd}`, retaining HDC as the default.
- Separate the runner-facing device behavior from platform-specific HDC and ABD command translation.
- Add common command-connection contracts and neutral command results so device and test code no longer depend on HDC types.
- **BREAKING** Rename the public HDC device factory from `lib.get_device` to `lib.get_hdc_device`.
- Support Andr0id package/activity launch, rooted smaps capture, logcat capture, screenshots, screen bounds, files, readiness, and approximate input replay.
- Replace HDC-specific internal log-capture operation names with generic names while retaining the existing `hilog/hilog.log` evidence layout and the `--device-log` CLI option.
- Rename `record.py` to `record_hdc.py` and document that recording remains OpenHarmony/HDC-only.
- Parse optional `HDC_PATH` and `ABD_PATH` values from `.env`; validate only the path required by the selected entry point.

## Capabilities

### New Capabilities
- `device-abd`: ABD-backed Andr0id device execution for benchmark flows.

### Modified Capabilities
- `device-hdc`: Generalize the device connection and log-capture contracts while preserving OpenHarmony HDC behavior.
- `benchmark-flow`: Select a device backend from the runner CLI and validate its configured executable path.
- `programmatic-interface`: Expose the backend-neutral device contract and additive ABD construction helpers.
- `ui-input-recording`: Rename the HDC-only recording entry point and document its backend limitation.

## Impact

Affected code includes `run.py`, `record.py`, `lib.py`, `src/device.py`, the HDC transport and device implementation, runner wiring, mocks, tests, and README documentation. New Python modules are added under `src/platform/ohos/` and `src/platform/andr0id/`, with the shared connection contract at `src/connection.py`. No third-party dependency is introduced.
