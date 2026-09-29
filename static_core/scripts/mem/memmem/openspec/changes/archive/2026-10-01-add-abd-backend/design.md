# Design

## Context

See proposal.md for motivation. The current concrete `Device` directly emits OpenHarmony HDC shell commands and returns HDC-named results. Runner and command code use an implicit device interface, while tests have both HDC and device fakes. Andr0id uses ABD and different device command grammars.

## Goals / Non-Goals

**Goals:**
- Isolate runner-facing device behavior from HDC and ABD command translation.
- Preserve existing HDC behavior and HDC CLI defaulting.
- Select one backend in `run.py` without requiring configuration for the other backend.
- Keep the existing benchmark flow, evidence paths, and public HDC helpers usable.

**Non-Goals:**
- Andr0id UI recording.
- Exact Andr0id equivalence for OpenHarmony gesture velocity, fling, drag, or step-length semantics.
- Andr0id battery or thermal discovery in this change.
- Multi-device ABD serial selection.

## Decisions

### Two contracts separate benchmark behavior from command execution

`src/device.py` will define the `Device` protocol and shared domain models, including the neutral `CommandResult`. Runner and command modules depend only on this protocol. `src/connection.py` will define `DeviceConnection` with `run`, `shell`, `shell_raw`, and `start_shell` for transport subprocess mechanics.

`Hdc` and `Abd` implement `DeviceConnection`. `HdcDevice` and `AbdDevice` implement `Device` and compose their matching connection. This prevents backend conditionals in runner code while avoiding an attempt to make ABD emulate HDC command syntax.

Alternative: inject a command-profile strategy into one concrete Device. Rejected because the device implementation would still combine platform behavior with shared runner behavior and make exact backend command testing indirect.

### Backend code follows platform packages

Platform-specific modules will be organized as:

```text
src/device.py
src/connection.py
src/platform/ohos/hdc.py
src/platform/ohos/hdc_device.py
src/platform/andr0id/abd.py
src/platform/andr0id/abd_device.py
```

Package initializer files make these importable. The qualified `src.platform` package does not shadow Python's top-level `platform` module.

### Config parsing is independent of backend selection

`Config` holds optional HDC and ABD executable paths. `load_config` normalizes missing and empty dotenv values to `None` and performs no backend validation. `run.py` validates the path selected by `--backend`; `record_hdc.py` validates HDC because it has no backend selection.

### Andr0id behavior uses native commands with bounded approximation

`AbdDevice` launches `package/activity` components, uses `am force-stop`, `wait-for-device`, `getprop sys.boot_completed`, `screencap`, `wm size`, `input`, and remote `logcat` streaming. It assumes a rooted device for smaps. It returns synthetic healthy device health until Andr0id health probing is separately designed. Andr0id's `input` CLI lacks the flow's distinct gesture and velocity model, so valid gestures remain executable with best-effort translation.

### Log operation names become transport-neutral

The Device contract and runner use generic log-capture method names. HDC continues to configure and stream hilog; ABD streams logcat. The `--device-log` flag controls capture while the `hilog/hilog.log` artifact layout remains unchanged for this scope.

### Programmatic compatibility is additive

Public `lib.get_hdc` remains available. The HDC device factory is renamed to `lib.get_hdc_device` to identify its backend explicitly, and additive ABD construction helpers and type exports are provided.

## Risks / Trade-offs

- [Rooted access is unavailable on a target device] -> Fail smaps collection with the underlying command error; rooted Andr0id is a documented prerequisite.
- [Andr0id input differs from UITest] -> Preserve flow validation and document best-effort replay rather than claim gesture fidelity.
- [Andr0id force-stop lacks a success marker] -> Treat a successful ABD invocation as successful termination by explicit product decision.
- [Synthetic Andr0id health hides actual thermal or battery conditions] -> Scope the bypass to this change and preserve the Device health API for future Andr0id discovery.
- [Renaming the recorder entry point breaks scripts] -> Document `record_hdc.py` as the replacement and update project tests and README atomically.

## Migration Plan

1. Move HDC implementation files and update imports while retaining public HDC helpers.
2. Introduce neutral contracts and adapt fakes before changing runner wiring.
3. Add Andr0id transport/device modules and exact command translation tests.
4. Add CLI selection and optional-path validation.
5. Rename and document the HDC-only recorder.

Rollback is selecting `--backend hdc`; the default remains HDC and no persisted format changes.
