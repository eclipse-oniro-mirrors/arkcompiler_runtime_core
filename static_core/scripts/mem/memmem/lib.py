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
    return Hdc(absolutize(pathlib.Path(hdc_exe_path)))


def get_hdc_device(hdc: Hdc) -> Device:
    return HdcDevice(hdc)


def get_abd(abd_exe_path: str | pathlib.Path) -> Abd:
    return Abd(absolutize(pathlib.Path(abd_exe_path)))


def get_abd_device(abd: Abd) -> Device:
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
    smaps_filter: re.Pattern[str] = MATCH_ALL,
) -> pathlib.Path:
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
               smaps_filter.pattern, smaps_filter.flags)
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
    return Flow(flow=apps, desc=desc)


def app_flow(
    label: str,
    bundle: str,
    ability: str,
    terminate: bool,
    commands: list[Command],
) -> AppFlow:
    return AppFlow(
        label=label,
        bundle=bundle,
        ability=ability,
        terminate=terminate,
        commands=commands,
    )


def wait(seconds: float) -> WaitCommand:
    return WaitCommand(action="wait", payload=seconds)


def snapshot(label: str) -> SnapshotCommand:
    return SnapshotCommand(action="snapshot", payload=label)


def screenshot(label: str) -> ScreenshotCommand:
    return ScreenshotCommand(action="screenshot", payload=label)


def key(name: typing.Literal["Home", "Back", "Power"]) -> KeyCommand:
    return KeyCommand(action="key", payload=NamedKeyPayload(key=name))


def tap(x_pct: int, y_pct: int) -> TapCommand:
    return TapCommand(action="tap", payload=TapPayload(x_pct=x_pct, y_pct=y_pct))


def double_tap(x_pct: int, y_pct: int) -> DoubleTapCommand:
    return DoubleTapCommand(
        action="double_tap", payload=TapPayload(x_pct=x_pct, y_pct=y_pct)
    )


def long_tap(x_pct: int, y_pct: int) -> LongTapCommand:
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
    return MultiTouchInputCommand(
        action="multi_touch_input",
        payload=MultiTouchInputPayload(
            finger_list=finger_list,
            duration_ms=duration_ms,
        ),
    )


def input_text(x_pct: int, y_pct: int, payload: str) -> InputTextCommand:
    return InputTextCommand(
        action="input_text",
        payload=InputTextPayload(x_pct=x_pct, y_pct=y_pct, text=payload),
    )


def text(payload: str) -> TextCommand:
    return TextCommand(action="text", payload=payload)


def unprocessed_flow(
    apps: list[UnprocessedAppFlow], desc: str | None = None
) -> UnprocessedFlow:
    return UnprocessedFlow(flow=apps, desc=desc)


def unprocessed_app_flow(
    label: str,
    bundle: str,
    ability: str,
    terminate: bool,
    commands: list[Macro | Command],
) -> UnprocessedAppFlow:
    return UnprocessedAppFlow(
        label=label,
        bundle=bundle,
        ability=ability,
        terminate=terminate,
        commands=commands,
    )


def unprocessed_snapshot(label: str) -> UnprocessedSnapshotCommand:
    return UnprocessedSnapshotCommand(action="snapshot", payload=label)


def unprocessed_screenshot(label: str) -> UnprocessedScreenshotCommand:
    return UnprocessedScreenshotCommand(action="screenshot", payload=label)


def repeat(
    n_iter: int,
    iter_var: str,
    commands: list[UnprocessedCommand],
) -> RepeatMacro:
    return RepeatMacro(
        macro="repeat",
        payload=RepeatMacroPayload(
            iter_var=iter_var, n_iter=n_iter, commands=commands),
    )


def preprocess_flow(uflow: UnprocessedFlow) -> Flow:
    validated_flow = UnprocessedFlow.model_validate(
        uflow.model_dump())
    return _preprocess_flow(validated_flow)
