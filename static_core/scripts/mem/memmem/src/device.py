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

import dataclasses
import pathlib
import subprocess
import typing


@dataclasses.dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str
    stderr: str

    def is_error(self) -> bool:
        return self.returncode != 0 or self.stderr != ""


@dataclasses.dataclass(frozen=True)
class ScreenBounds:
    left: int
    top: int
    right: int
    bottom: int


@dataclasses.dataclass(frozen=True)
class ThermalZoneHealth:
    name: str
    temp_millis_celsius: int


@dataclasses.dataclass(frozen=True)
class DeviceHealth:
    battery_capacity_pct: int
    thermal_zones: list[ThermalZoneHealth]


@dataclasses.dataclass(frozen=True)
class Point:
    x: int
    y: int

    @staticmethod
    def from_normalized(bounds: ScreenBounds, x_pct: int, y_pct: int) -> "Point":
        width = bounds.right - bounds.left
        height = bounds.bottom - bounds.top
        if width <= 1 or height <= 1:
            raise ValueError("screen bounds must describe a non-empty area")
        x = bounds.left + round(width * x_pct / 100)
        y = bounds.top + round(height * y_pct / 100)
        return Point(
            x=min(max(x, bounds.left + 1), bounds.right - 1),
            y=min(max(y, bounds.top + 1), bounds.bottom - 1),
        )


class Device(typing.Protocol):
    def launch_app(self, bundle: str, ability: str) -> None:
        ...

    def terminate_app(self, bundle: str) -> None:
        ...

    def reboot(self) -> None:
        ...

    def wait_available(self, timeout_seconds: int) -> None:
        ...

    def wait_boot_completed(self, timeout_seconds: int,
                            poll_interval_seconds: int = 1) -> None:
        ...

    def wakeup(self) -> None:
        ...

    def disable_screen_timeout(self) -> None:
        ...

    def device_health(self) -> DeviceHealth:
        ...

    def configure_log_capture(self) -> None:
        ...

    def start_log_capture(
            self, remote_path: pathlib.PurePosixPath) -> subprocess.Popen[typing.Any]:
        ...

    def resolve_pid(self, bundle: str) -> int:
        ...

    def pid_exists(self, pid: int) -> bool:
        ...

    def timestamp(self) -> str:
        ...

    def make_dir(self, remote_path: pathlib.PurePosixPath) -> bool:
        ...

    def remove_dir(self, remote_path: pathlib.PurePosixPath) -> bool:
        ...

    def capture_smaps(
            self, pid: int, remote_path: pathlib.PurePosixPath) -> bool:
        ...

    def capture_screenshot(
            self, remote_path: pathlib.PurePosixPath) -> bool:
        ...

    def screen_bounds(self) -> ScreenBounds:
        ...

    def tap(self, point: Point) -> None:
        ...

    def double_tap(self, point: Point) -> None:
        ...

    def long_tap(self, point: Point) -> None:
        ...

    def swipe(self, start: Point, end: Point, velocity: int) -> None:
        ...

    def drag(self, start: Point, end: Point, velocity: int) -> None:
        ...

    def fling(self, start: Point, end: Point,
              velocity: int, step_length: int) -> None:
        ...

    def directional_fling(self, direction: str,
                          velocity: int, step_length: int) -> None:
        ...

    def input_text(self, point: Point, text: str) -> None:
        ...

    def text(self, text: str) -> None:
        ...

    def send_key(self, key: str) -> None:
        ...

    def send_file(self, local_path: pathlib.Path,
                  remote_path: pathlib.PurePosixPath) -> CommandResult:
        ...

    def recv_file(self, remote_path: pathlib.PurePosixPath,
                  local_path: pathlib.Path) -> CommandResult:
        ...
