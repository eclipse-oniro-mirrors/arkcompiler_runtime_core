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

import typing

import pydantic

from src.pydantic_helpers import PydanticModel


_LABEL_PATTERN = r"^[A-Za-z0-9_-]+$"
_ITER_VAR_PATTERN = r"^[A-Za-z_][A-Za-z0-9_]*$"

Label = typing.Annotated[
    str, pydantic.StringConstraints(pattern=_LABEL_PATTERN, strict=True)]
IterVar = typing.Annotated[
    str, pydantic.StringConstraints(pattern=_ITER_VAR_PATTERN, strict=True)]

Percentile = typing.Annotated[int, pydantic.Field(ge=0, le=100, strict=True)]
Velocity = typing.Annotated[int, pydantic.Field(ge=200, le=40000, strict=True)]
StepLength = typing.Annotated[int, pydantic.Field(gt=0, strict=True)]
WaitPayload = typing.Annotated[float, pydantic.Field(ge=0, strict=True)]
TextPayload = typing.Annotated[str, pydantic.Field(min_length=1, strict=True)]


PayloadT = typing.TypeVar("PayloadT")


class _CommandBase(PydanticModel, typing.Generic[PayloadT]):

    payload: PayloadT


class WaitCommand(_CommandBase[WaitPayload]):
    action: typing.Literal["wait"]


class SnapshotCommand(_CommandBase[Label]):
    action: typing.Literal["snapshot"]


class ScreenshotCommand(_CommandBase[Label]):
    action: typing.Literal["screenshot"]


class TapPayload(PydanticModel):

    x_pct: Percentile
    y_pct: Percentile


class Displacement(PydanticModel):

    x1_pct: Percentile
    y1_pct: Percentile
    x2_pct: Percentile
    y2_pct: Percentile


class SwipePayload(Displacement):
    velocity: Velocity


class FlingPayload(SwipePayload):
    step_length: StepLength


class DirectionalFlingPayload(PydanticModel):

    direction: typing.Literal["left", "right", "up", "down"]
    velocity: Velocity
    step_length: StepLength


class InputTextPayload(TapPayload):
    text: TextPayload


class MultiTouchInputPayload(PydanticModel):

    finger_list: (
        tuple[Displacement, Displacement]
        | tuple[Displacement, Displacement, Displacement]
    )
    duration_ms: typing.Annotated[
        int, pydantic.Field(ge=1, le=15000, strict=True)]


class NamedKeyPayload(PydanticModel):

    key: typing.Literal["Home", "Back", "Power"]


class KeyCommand(_CommandBase[NamedKeyPayload]):
    action: typing.Literal["key"]


class TapCommand(_CommandBase[TapPayload]):
    action: typing.Literal["tap"]


class DoubleTapCommand(_CommandBase[TapPayload]):
    action: typing.Literal["double_tap"]


class LongTapCommand(_CommandBase[TapPayload]):
    action: typing.Literal["long_tap"]


class SwipeCommand(_CommandBase[SwipePayload]):
    action: typing.Literal["swipe"]


class DragCommand(_CommandBase[SwipePayload]):
    action: typing.Literal["drag"]


class FlingCommand(_CommandBase[FlingPayload]):
    action: typing.Literal["fling"]


class DirectionalFlingCommand(_CommandBase[DirectionalFlingPayload]):
    action: typing.Literal["directional_fling"]


class InputTextCommand(_CommandBase[InputTextPayload]):
    action: typing.Literal["input_text"]


class TextCommand(_CommandBase[TextPayload]):
    action: typing.Literal["text"]


class MultiTouchInputCommand(_CommandBase[MultiTouchInputPayload]):
    action: typing.Literal["multi_touch_input"]


Command = typing.Annotated[
    WaitCommand
    | SnapshotCommand
    | ScreenshotCommand
    | KeyCommand
    | TapCommand
    | DoubleTapCommand
    | LongTapCommand
    | SwipeCommand
    | DragCommand
    | FlingCommand
    | DirectionalFlingCommand
    | InputTextCommand
    | TextCommand
    | MultiTouchInputCommand,
    pydantic.Field(discriminator="action"),
]


class AppFlow(PydanticModel):

    label: Label
    bundle: str
    ability: str
    terminate: bool
    commands: list[Command]


class Flow(PydanticModel):
    model_config = pydantic.ConfigDict(
        serialize_by_alias=True, validate_by_name=True)

    desc: pydantic.StrictStr | None = pydantic.Field(
        default=None,
        validation_alias="$desc",
        serialization_alias="$desc",
        exclude_if=lambda value: value is None,
    )
    flow: list[AppFlow]

    @pydantic.model_validator(mode="after")
    def validate_unique_labels(self) -> "Flow":
        app_labels = [app.label for app in self.flow]
        if len(app_labels) != len(set(app_labels)):
            raise ValueError("duplicate app label")

        snapshot_labels = [
            command.payload
            for app in self.flow
            for command in app.commands
            if isinstance(command, SnapshotCommand)
        ]
        if len(snapshot_labels) != len(set(snapshot_labels)):
            raise ValueError("duplicate snapshot label")

        screenshot_labels = [
            command.payload
            for app in self.flow
            for command in app.commands
            if isinstance(command, ScreenshotCommand)
        ]
        if len(screenshot_labels) != len(set(screenshot_labels)):
            raise ValueError("duplicate screenshot label")
        return self


class UnprocessedSnapshotCommand(_CommandBase[str]):
    action: typing.Literal["snapshot"]


class UnprocessedScreenshotCommand(_CommandBase[str]):
    action: typing.Literal["screenshot"]


UnprocessedCommand = typing.Annotated[
    WaitCommand
    | UnprocessedSnapshotCommand
    | UnprocessedScreenshotCommand
    | KeyCommand
    | TapCommand
    | DoubleTapCommand
    | LongTapCommand
    | SwipeCommand
    | DragCommand
    | FlingCommand
    | DirectionalFlingCommand
    | InputTextCommand
    | TextCommand
    | MultiTouchInputCommand,
    pydantic.Field(discriminator="action"),
]


class _MacroBase(PydanticModel, typing.Generic[PayloadT]):

    payload: PayloadT


class RepeatMacroPayload(PydanticModel):

    iter_var: IterVar
    n_iter: typing.Annotated[int, pydantic.Field(ge=0, strict=True)]
    commands: list[UnprocessedCommand]


class RepeatMacro(_MacroBase[RepeatMacroPayload]):
    macro: typing.Literal["repeat"]


Macro = typing.Annotated[RepeatMacro, pydantic.Field(discriminator="macro")]


class UnprocessedAppFlow(PydanticModel):

    label: str
    bundle: str
    ability: str
    terminate: bool
    commands: list[Macro | Command]


class UnprocessedFlow(PydanticModel):
    model_config = pydantic.ConfigDict(
        serialize_by_alias=True, validate_by_name=True)

    desc: pydantic.StrictStr | None = pydantic.Field(
        default=None,
        validation_alias="$desc",
        serialization_alias="$desc",
        exclude_if=lambda value: value is None,
    )
    flow: list[UnprocessedAppFlow]
