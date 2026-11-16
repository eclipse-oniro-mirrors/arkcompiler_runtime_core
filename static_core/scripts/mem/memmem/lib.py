#!/usr/bin/env python3
# -- coding: utf-8 --
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

__all__ = [
    "AppFlow",
    "Command",
    "Device",
    "DirectionalFlingCommand",
    "DoubleTapCommand",
    "DragCommand",
    "FlingCommand",
    "Flow",
    "Hdc",
    "Abd",
    "InputTextCommand",
    "KeyCommand",
    "LongTapCommand",
    "Macro",
    "MultiTouchInputCommand",
    "RepeatMacro",
    "ScreenshotCommand",
    "SnapshotCommand",
    "SwipeCommand",
    "TapCommand",
    "TextCommand",
    "UnprocessedAppFlow",
    "UnprocessedFlow",
    "UnprocessedScreenshotCommand",
    "UnprocessedSnapshotCommand",
    "WaitCommand",
    "app_flow",
    "configure_logger",
    "directional_fling",
    "double_tap",
    "drag",
    "fling",
    "flow",
    "get_hdc_device",
    "get_hdc",
    "get_abd",
    "get_abd_device",
    "get_logger",
    "input_text",
    "key",
    "long_tap",
    "multi_touch_input",
    "preprocess_flow",
    "repeat",
    "reset_logger",
    "execute_flow",
    "create_view",
    "screenshot",
    "snapshot",
    "swipe",
    "tap",
    "text",
    "unprocessed_app_flow",
    "unprocessed_flow",
    "unprocessed_screenshot",
    "unprocessed_snapshot",
    "wait",
]

import pathlib
import re
import typing

from src.device import Device
from src.platform.andr0id.abd import Abd
from src.platform.andr0id.abd_device import AbdDevice
from src.platform.ohos.hdc import Hdc
from src.platform.ohos.hdc_device import HdcDevice
from src.log import configure_logger, get_logger, reset_logger
from src.preprocess import preprocess_flow as _preprocess_flow
from src.path import absolutize
from src.evidence import ParsedEvidence, is_evidence_equivalent, parse_evidence
from src.metadata import AppSnapshotLabelPair
from src.report import write_view
from src.store import EvidenceStore, ViewStore
from src.smaps import MATCH_ALL
from src.runner import BenchmarkOptions, create_evidence_store, run_benchmark
from src.schema import (
    AppFlow,
    Command,
    DirectionalFlingCommand,
    DirectionalFlingPayload,
    DoubleTapCommand,
    DragCommand,
    FlingCommand,
    FlingPayload,
    Flow,
    InputTextCommand,
    InputTextPayload,
    KeyCommand,
    LongTapCommand,
    Macro,
    MultiTouchInputCommand,
    MultiTouchInputPayload,
    NamedKeyPayload,
    RepeatMacro,
    RepeatMacroPayload,
    ScreenshotCommand,
    SnapshotCommand,
    SwipeCommand,
    SwipePayload,
    TapCommand,
    TapPayload,
    TextCommand,
    UnprocessedAppFlow,
    UnprocessedCommand,
    UnprocessedFlow,
    UnprocessedScreenshotCommand,
    UnprocessedSnapshotCommand,
    WaitCommand,
)


def get_hdc(hdc_exe_path: str | pathlib.Path) -> Hdc:
    """@brief Creates an HDC handle using a lexically normalized executable path.

    @param hdc_exe_path str | pathlib.Path: HDC executable path resolved relative to the caller's working directory when relative.
    @return Hdc: Handle for constructing an HDC-backed device; no process is started.
    """
    return Hdc(absolutize(pathlib.Path(hdc_exe_path)))


def get_hdc_device(hdc: Hdc) -> Device:
    """@brief Creates a Device facade backed by an HDC handle.

    @param hdc Hdc: Configured HDC handle used for future device commands.
    @return Device: HDC-backed device facade; no device command is issued.
    """
    return HdcDevice(hdc)


def get_abd(abd_exe_path: str | pathlib.Path) -> Abd:
    """@brief Creates an ABD handle using a lexically normalized executable path.

    @param abd_exe_path str | pathlib.Path: ABD executable path resolved relative to the caller's working directory when relative.
    @return Abd: Handle for constructing an ABD-backed device; no process is started.
    """
    return Abd(absolutize(pathlib.Path(abd_exe_path)))


def get_abd_device(abd: Abd) -> Device:
    """@brief Creates a Device facade backed by an ABD handle.

    @param abd Abd: Configured ABD handle used for future device commands.
    @return Device: ABD-backed device facade; no device command is issued.
    """
    return AbdDevice(abd)


def _validate_command_support(f: Flow, d: Device) -> None:
    for af in f.flow:
        for command in af.commands:
            if not d.is_command_supported(command):
                raise RuntimeError(
                    "device does not support command: "
                    f"app={af.label} action={command.action}"
                )


def execute_flow(
    f: Flow,
    d: Device,
    dst: pathlib.Path,
    *,
    reboot: bool = False,
    device_log: bool = True,
) -> pathlib.Path:
    """@brief Executes one canonical flow and returns its normalized evidence destination. Creates the exact new destination, performs device work, and retains newly created partial evidence on failure; for layout details, refer to README.md#evidence.

    @param f Flow: Canonical flow revalidated before execution.
    @param d Device: Target device used for command-support checks and collection.
    @param dst pathlib.Path: New evidence destination resolved lexically against the caller's working directory. It must not already exist.
    @param reboot bool: Whether to reboot the device before collection.
    @param device_log bool: Whether to collect device logs in the evidence.
    @return pathlib.Path: Normalized absolute evidence destination after successful execution.
    """
    validated_flow = Flow.model_validate(f.model_dump())
    _validate_command_support(validated_flow, d)
    resolved_dst = absolutize(dst)
    options = BenchmarkOptions(
        out_dir=resolved_dst,
        reboot=reboot,
        device_log=device_log,
    )
    run_benchmark(validated_flow, options,
                  create_evidence_store(options), d)
    return resolved_dst


def create_view(
    dirs: list[pathlib.Path],
    dst: pathlib.Path,
    *,
    smaps_filter: re.Pattern[str] = MATCH_ALL,
    plots: bool = True,
) -> pathlib.Path:
    """@brief Creates one view from ordered compatible evidence sources and returns its normalized destination. Creates the exact new destination, retains newly created partial output on failure, and does not access devices or modify evidence; for layout details, refer to README.md#view.

    @param dirs list[pathlib.Path]: Nonempty ordered evidence source paths. Normalized duplicate paths are rejected.
    @param dst pathlib.Path: New view destination resolved lexically against the caller's working directory. It must not already exist.
    @param smaps_filter re.Pattern[str]: Compiled string pattern used to select normalized smaps tags. None, bytes, and non-pattern values are rejected.
    @param plots bool: Whether to generate SVG plots. False still creates an empty plots directory.
    @return pathlib.Path: Normalized absolute view destination after successful generation.
    """
    if not dirs:
        raise ValueError("at least one evidence directory is required")
    if not isinstance(smaps_filter, re.Pattern) or not isinstance(smaps_filter.pattern, str):
        raise TypeError("smaps_filter must be a compiled string pattern")
    sources = [absolutize(path) for path in dirs]
    if len(sources) != len(set(sources)):
        raise ValueError("duplicate evidence directory")
    parsed = [parse_evidence(EvidenceStore(source), smaps_filter)
              for source in sources]
    _warn_unmatched_smaps(parsed, smaps_filter)
    if any(not is_evidence_equivalent(parsed[0], item) for item in parsed[1:]):
        raise ValueError("incompatible evidence directories")
    destination = absolutize(dst)
    write_view(parsed, ViewStore(destination),
               smaps_filter.pattern, smaps_filter.flags, plots)
    return destination


def _warn_unmatched_smaps(evidence: list[ParsedEvidence],
                          smaps_filter: re.Pattern[str]) -> None:
    for item in evidence:
        _warn_unmatched_smaps_for_evidence(item, smaps_filter)


def _warn_unmatched_smaps_for_evidence(
    item: ParsedEvidence,
    smaps_filter: re.Pattern[str],
) -> None:
    snapshots_dir = EvidenceStore(item.source).local_snapshots_dir()
    for artifact in item.snapshots.artifacts:
        for file in artifact.files:
            _warn_if_smaps_unmatched(item, artifact.label, file,
                                     snapshots_dir, smaps_filter)


def _warn_if_smaps_unmatched(
    item: ParsedEvidence,
    snapshot_label: str,
    file: str,
    snapshots_dir: pathlib.Path,
    smaps_filter: re.Pattern[str],
) -> None:
    relative_path = pathlib.PurePosixPath(file)
    pair = AppSnapshotLabelPair(relative_path.stem, snapshot_label)
    if pair not in item.measurements:
        path = snapshots_dir.joinpath(*relative_path.parts)
        get_logger().warn(
            f"smaps file {path} has no mappings aggregated for pattern {smaps_filter.pattern}")


def flow(apps: list[AppFlow], desc: str | None = None) -> Flow:
    """@brief Constructs a validated canonical flow without executing it or performing I/O.

    @param apps list[AppFlow]: Ordered canonical application flows. App labels and artifact labels must be unique across the flow.
    @param desc str | None: Optional human-readable flow description that does not affect execution.
    @return Flow: Validated canonical flow ready for execute_flow.
    """
    return Flow(flow=apps, desc=desc)


def app_flow(
    label: str,
    bundle: str,
    ability: str,
    terminate: bool,
    commands: list[Command],
) -> AppFlow:
    """@brief Constructs a validated canonical application flow without launching an application.

    @param label str: Application label containing only letters, digits, underscores, and hyphens. flow checks uniqueness across applications.
    @param bundle str: Application bundle identifier used during execution.
    @param ability str: Application ability identifier used during execution.
    @param terminate bool: Whether execution terminates the application after its commands.
    @param commands list[Command]: Ordered canonical commands for this application flow.
    @return AppFlow: Validated canonical application flow.
    """
    return AppFlow(
        label=label,
        bundle=bundle,
        ability=ability,
        terminate=terminate,
        commands=commands,
    )


def wait(seconds: float) -> WaitCommand:
    """@brief Constructs a validated wait command without delaying the caller.

    @param seconds float: Wait duration in seconds. It must be nonnegative.
    @return WaitCommand: Canonical command that delays execution when its flow runs.
    """
    return WaitCommand(action="wait", payload=seconds)


def snapshot(label: str) -> SnapshotCommand:
    """@brief Constructs a validated snapshot command without capturing device memory.

    @param label str: Snapshot label containing only letters, digits, underscores, and hyphens. It must be unique across the flow.
    @return SnapshotCommand: Canonical command that captures active-app smaps when its flow runs.
    """
    return SnapshotCommand(action="snapshot", payload=label)


def screenshot(label: str) -> ScreenshotCommand:
    """@brief Constructs a validated screenshot command without capturing the device screen.

    @param label str: Screenshot label containing only letters, digits, underscores, and hyphens. It must be unique across the flow.
    @return ScreenshotCommand: Canonical command that captures PNG evidence when its flow runs.
    """
    return ScreenshotCommand(action="screenshot", payload=label)


def key(name: typing.Literal["Home", "Back", "Power"]) -> KeyCommand:
    """@brief Constructs a validated named-key command without sending input to a device.

    @param name Literal["Home", "Back", "Power"]: Named key to send. Only Home, Back, and Power are accepted.
    @return KeyCommand: Canonical command that sends the selected key when its flow runs.
    """
    return KeyCommand(action="key", payload=NamedKeyPayload(key=name))


def tap(x_pct: int, y_pct: int) -> TapCommand:
    """@brief Constructs a validated tap command without sending input to a device.

    @param x_pct int: Horizontal coordinate as an integer percentage from 0 through 100.
    @param y_pct int: Vertical coordinate as an integer percentage from 0 through 100.
    @return TapCommand: Canonical command that taps the coordinate when its flow runs.
    """
    return TapCommand(action="tap", payload=TapPayload(x_pct=x_pct, y_pct=y_pct))


def double_tap(x_pct: int, y_pct: int) -> DoubleTapCommand:
    """@brief Constructs a validated double-tap command without sending input to a device.

    @param x_pct int: Horizontal coordinate as an integer percentage from 0 through 100.
    @param y_pct int: Vertical coordinate as an integer percentage from 0 through 100.
    @return DoubleTapCommand: Canonical command that double-taps the coordinate when its flow runs.
    """
    return DoubleTapCommand(
        action="double_tap", payload=TapPayload(x_pct=x_pct, y_pct=y_pct)
    )


def long_tap(x_pct: int, y_pct: int) -> LongTapCommand:
    """@brief Constructs a validated long-tap command without sending input to a device.

    @param x_pct int: Horizontal coordinate as an integer percentage from 0 through 100.
    @param y_pct int: Vertical coordinate as an integer percentage from 0 through 100.
    @return LongTapCommand: Canonical command that long-taps the coordinate when its flow runs.
    """
    return LongTapCommand(
        action="long_tap", payload=TapPayload(x_pct=x_pct, y_pct=y_pct)
    )


def swipe(
    x1_pct: int,
    y1_pct: int,
    x2_pct: int,
    y2_pct: int,
    velocity: int,
) -> SwipeCommand:
    """@brief Constructs a validated swipe command without sending input to a device.

    @param x1_pct int: Start horizontal coordinate as an integer percentage from 0 through 100.
    @param y1_pct int: Start vertical coordinate as an integer percentage from 0 through 100.
    @param x2_pct int: End horizontal coordinate as an integer percentage from 0 through 100.
    @param y2_pct int: End vertical coordinate as an integer percentage from 0 through 100.
    @param velocity int: Swipe velocity from 200 through 40000.
    @return SwipeCommand: Canonical command that swipes the coordinates when its flow runs.
    """
    return SwipeCommand(
        action="swipe",
        payload=SwipePayload(
            x1_pct=x1_pct,
            y1_pct=y1_pct,
            x2_pct=x2_pct,
            y2_pct=y2_pct,
            velocity=velocity,
        ),
    )


def drag(
    x1_pct: int,
    y1_pct: int,
    x2_pct: int,
    y2_pct: int,
    velocity: int,
) -> DragCommand:
    """@brief Constructs a validated drag command without sending input to a device.

    @param x1_pct int: Start horizontal coordinate as an integer percentage from 0 through 100.
    @param y1_pct int: Start vertical coordinate as an integer percentage from 0 through 100.
    @param x2_pct int: End horizontal coordinate as an integer percentage from 0 through 100.
    @param y2_pct int: End vertical coordinate as an integer percentage from 0 through 100.
    @param velocity int: Drag velocity from 200 through 40000.
    @return DragCommand: Canonical command that drags between the coordinates when its flow runs.
    """
    return DragCommand(
        action="drag",
        payload=SwipePayload(
            x1_pct=x1_pct,
            y1_pct=y1_pct,
            x2_pct=x2_pct,
            y2_pct=y2_pct,
            velocity=velocity,
        ),
    )


def fling(
    x1_pct: int,
    y1_pct: int,
    x2_pct: int,
    y2_pct: int,
    velocity: int,
    step_length: int,
) -> FlingCommand:
    """@brief Constructs a validated fling command without sending input to a device.

    @param x1_pct int: Start horizontal coordinate as an integer percentage from 0 through 100.
    @param y1_pct int: Start vertical coordinate as an integer percentage from 0 through 100.
    @param x2_pct int: End horizontal coordinate as an integer percentage from 0 through 100.
    @param y2_pct int: End vertical coordinate as an integer percentage from 0 through 100.
    @param velocity int: Fling velocity from 200 through 40000.
    @param step_length int: Positive fling step length.
    @return FlingCommand: Canonical command that flings between the coordinates when its flow runs.
    """
    return FlingCommand(
        action="fling",
        payload=FlingPayload(
            x1_pct=x1_pct,
            y1_pct=y1_pct,
            x2_pct=x2_pct,
            y2_pct=y2_pct,
            velocity=velocity,
            step_length=step_length,
        ),
    )


def directional_fling(
    direction: typing.Literal["left", "right", "up", "down"],
    velocity: int,
    step_length: int,
) -> DirectionalFlingCommand:
    """@brief Constructs a validated directional fling command without sending input to a device.

    @param direction Literal["left", "right", "up", "down"]: Fling direction. Only left, right, up, and down are accepted.
    @param velocity int: Fling velocity from 200 through 40000.
    @param step_length int: Positive fling step length.
    @return DirectionalFlingCommand: Canonical command that flings in the selected direction when its flow runs.
    """
    return DirectionalFlingCommand(
        action="directional_fling",
        payload=DirectionalFlingPayload(
            direction=direction,
            velocity=velocity,
            step_length=step_length,
        ),
    )


def multi_touch_input(
    finger_list: list[dict[str, int]], duration_ms: int
) -> MultiTouchInputCommand:
    """@brief Constructs a validated concurrent multi-touch command without sending input to a device.

    @param finger_list list[dict[str, int]]: Two or three normalized finger paths with integer percentage coordinates from 0 through 100.
    @param duration_ms int: Gesture duration in milliseconds from 1 through 15000.
    @return MultiTouchInputCommand: Canonical command supported by HDC execution and rejected by ABD execution.
    """
    return MultiTouchInputCommand(
        action="multi_touch_input",
        payload=MultiTouchInputPayload.model_validate({
            "finger_list": finger_list,
            "duration_ms": duration_ms,
        }),
    )


def input_text(x_pct: int, y_pct: int, payload: str) -> InputTextCommand:
    """@brief Constructs a validated text-input command without sending input to a device.

    @param x_pct int: Horizontal input coordinate as an integer percentage from 0 through 100.
    @param y_pct int: Vertical input coordinate as an integer percentage from 0 through 100.
    @param payload str: Nonempty text to enter when the flow runs.
    @return InputTextCommand: Canonical command that enters text at the coordinate when its flow runs.
    """
    return InputTextCommand(
        action="input_text",
        payload=InputTextPayload(x_pct=x_pct, y_pct=y_pct, text=payload),
    )


def text(payload: str) -> TextCommand:
    """@brief Constructs a validated text command without sending input to a device.

    @param payload str: Nonempty text to enter when the flow runs.
    @return TextCommand: Canonical command that enters text through the active application when its flow runs.
    """
    return TextCommand(action="text", payload=payload)


def unprocessed_flow(
    apps: list[UnprocessedAppFlow], desc: str | None = None
) -> UnprocessedFlow:
    """@brief Constructs a validated unprocessed flow that may contain macros, without expanding macros or performing I/O.

    @param apps list[UnprocessedAppFlow]: Ordered application flows whose commands may include macros.
    @param desc str | None: Optional human-readable flow description that does not affect preprocessing or execution.
    @return UnprocessedFlow: Validated macro-capable flow ready for preprocess_flow.
    """
    return UnprocessedFlow(flow=apps, desc=desc)


def unprocessed_app_flow(
    label: str,
    bundle: str,
    ability: str,
    terminate: bool,
    commands: list[Macro | Command],
) -> UnprocessedAppFlow:
    """@brief Constructs a validated macro-capable application flow without launching an application.

    @param label str: Application label, validated as a string before canonical validation during preprocessing.
    @param bundle str: Application bundle identifier used after preprocessing and execution.
    @param ability str: Application ability identifier used after preprocessing and execution.
    @param terminate bool: Whether execution terminates the application after its commands.
    @param commands list[Macro | Command]: Ordered canonical commands and repeat macros.
    @return UnprocessedAppFlow: Validated macro-capable application flow.
    """
    return UnprocessedAppFlow(
        label=label,
        bundle=bundle,
        ability=ability,
        terminate=terminate,
        commands=commands,
    )


def unprocessed_snapshot(label: str) -> UnprocessedSnapshotCommand:
    """@brief Constructs a macro-expandable snapshot command without capturing device memory.

    @param label str: Snapshot label template; repeat placeholders are expanded by preprocess_flow before canonical validation.
    @return UnprocessedSnapshotCommand: Unprocessed snapshot command ready for macro expansion.
    """
    return UnprocessedSnapshotCommand(action="snapshot", payload=label)


def unprocessed_screenshot(label: str) -> UnprocessedScreenshotCommand:
    """@brief Constructs a macro-expandable screenshot command without capturing the device screen.

    @param label str: Screenshot label template; repeat placeholders are expanded by preprocess_flow before canonical validation.
    @return UnprocessedScreenshotCommand: Unprocessed screenshot command ready for macro expansion.
    """
    return UnprocessedScreenshotCommand(action="screenshot", payload=label)


def repeat(
    n_iter: int,
    iter_var: str,
    commands: list[UnprocessedCommand],
) -> RepeatMacro:
    """@brief Constructs a validated repeat macro without expanding commands or performing I/O.

    @param n_iter int: Number of expansions. It must be a nonnegative integer.
    @param iter_var str: Iteration variable matching a Python-style identifier.
    @param commands list[UnprocessedCommand]: Macro-capable command body expanded once per iteration.
    @return RepeatMacro: Unprocessed macro ready for preprocess_flow.
    """
    return RepeatMacro(
        macro="repeat",
        payload=RepeatMacroPayload(
            iter_var=iter_var, n_iter=n_iter, commands=commands),
    )


def preprocess_flow(uflow: UnprocessedFlow) -> Flow:
    """@brief Revalidates and expands macros into a canonical flow without performing filesystem or device operations.

    @param uflow UnprocessedFlow: Macro-capable flow. Repeat placeholders expand before canonical flow validation.
    @return Flow: Validated canonical flow suitable for execute_flow; invalid expanded labels or duplicate artifacts raise validation errors.
    """
    validated_flow = UnprocessedFlow.model_validate(
        uflow.model_dump())
    return _preprocess_flow(validated_flow)
