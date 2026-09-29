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

import math
import pathlib
import re
import shlex
import subprocess
import time
import typing

from src.device import (CommandResult, Device, DeviceHealth, Point, ScreenBounds,
                        ThermalZoneHealth)
from src.platform.andr0id.abd import Abd


_SIZE_RE = re.compile(r"(\d+)x(\d+)")
_BATTERY_LEVEL_RE = re.compile(r"^\s*level:\s*(\d+)\s*$", re.MULTILINE)
_THERMAL_TEMPERATURE_RE = re.compile(
    r"Temperature\{mValue=([-+]?\d+(?:\.\d+)?),.*?mName=([^,}]+),"
)
_MIN_SWIPE_DURATION_MS = 150
_MAX_SWIPE_DURATION_MS = 500
_FLING_DURATION_MS = 120


def _raise_if_error(result: CommandResult, fallback: str) -> None:
    if result.is_error():
        raise RuntimeError(result.stderr.strip()
                           or result.stdout.strip() or fallback)


def _swipe_duration(start: Point, end: Point, velocity: int) -> int:
    distance = math.hypot(end.x - start.x, end.y - start.y)
    duration_ms = round(distance * 1000 / velocity)
    return min(max(duration_ms, _MIN_SWIPE_DURATION_MS),
               _MAX_SWIPE_DURATION_MS)


class AbdDevice(Device):
    def __init__(self, abd: Abd) -> None:
        self.abd = abd

    def launch_app(self, bundle: str, ability: str) -> None:
        result = self.abd.shell("am", "start", "-W",
                                "-n", f"{bundle}/{ability}")
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip()
                               or result.stdout.strip() or "app launch failed")

    def terminate_app(self, bundle: str) -> None:
        _raise_if_error(self.abd.shell("am", "force-stop",
                        bundle), "app termination failed")

    def reboot(self) -> None:
        _raise_if_error(self.abd.run("reboot"), "device reboot failed")

    def wait_available(self, timeout_seconds: int) -> None:
        _raise_if_error(self.abd.run("wait-for-device",
                        timeout=timeout_seconds), "device wait failed")

    def wait_boot_completed(self, timeout_seconds: int, poll_interval_seconds: int = 1) -> None:
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            result = self.abd.shell("getprop", "sys.boot_completed")
            if not result.is_error() and result.stdout.strip() == "1":
                return
            time.sleep(poll_interval_seconds)
        raise RuntimeError("device boot did not complete before timeout")

    def wakeup(self) -> None:
        _raise_if_error(self.abd.shell("input", "keyevent",
                        "KEYCODE_WAKEUP"), "wakeup failed")

    def disable_screen_timeout(self) -> None:
        _raise_if_error(
            self.abd.shell("settings", "put", "system",
                           "screen_off_timeout", "60000000"),
            "screen timeout setup failed",
        )

    def device_health(self) -> DeviceHealth:
        battery = self.abd.shell("dumpsys", "battery")
        _raise_if_error(battery, "battery capacity failed")
        capacity_match = _BATTERY_LEVEL_RE.search(battery.stdout)
        if capacity_match is None:
            raise RuntimeError("battery capacity is malformed")
        capacity = int(capacity_match.group(1))
        if not 0 <= capacity <= 100:
            raise RuntimeError("battery capacity is malformed")

        thermal = self.abd.shell("dumpsys", "thermalservice")
        _raise_if_error(thermal, "thermal zones failed")
        current_marker = "Current temperatures from HAL:"
        cooling_marker = "Current cooling devices from HAL:"
        current = thermal.stdout.partition(current_marker)[2]
        current = current.partition(cooling_marker)[0]
        zones = [
            ThermalZoneHealth(
                name=name, temp_millis_celsius=round(float(value) * 1000))
            for value, name in _THERMAL_TEMPERATURE_RE.findall(current)
        ]
        if not zones:
            raise RuntimeError(
                "thermal zones failed: no HAL temperatures found")
        return DeviceHealth(battery_capacity_pct=capacity, thermal_zones=zones)

    def configure_log_capture(self) -> None:
        return None

    def start_log_capture(self, remote_path: pathlib.PurePosixPath) -> subprocess.Popen[typing.Any]:
        return self.abd.start_shell(f"logcat > {shlex.quote(str(remote_path))}")

    def resolve_pid(self, bundle: str) -> int:
        result = self.abd.shell("pidof", bundle)
        _raise_if_error(result, "pid lookup failed")
        pids = [int(field)
                for field in result.stdout.split() if field.isdigit()]
        if len(pids) == 1:
            return pids[0]
        if len(pids) > 1:
            raise RuntimeError(
                f"multiple PIDs resolved for bundle {bundle}: {pids}")
        raise RuntimeError(f"could not resolve PID for bundle {bundle}")

    def pid_exists(self, pid: int) -> bool:
        result = self.abd.shell_raw(
            f"[ -d /proc/{pid} ] && echo yes || echo no")
        return not result.is_error() and result.stdout.strip() == "yes"

    def timestamp(self) -> str:
        result = self.abd.shell("date", "+%s%N")
        if not result.is_error() and result.stdout.strip().isdigit():
            return result.stdout.strip()
        result = self.abd.shell("date", "+%s")
        if not result.is_error() and result.stdout.strip().isdigit():
            return f"{result.stdout.strip()}000000000"
        raise RuntimeError(result.stderr.strip() or "device timestamp failed")

    def make_dir(self, remote_path: pathlib.PurePosixPath) -> bool:
        return not self.abd.shell("mkdir", "-p", str(remote_path)).is_error()

    def remove_dir(self, remote_path: pathlib.PurePosixPath) -> bool:
        return not self.abd.shell("rm", "-rf", str(remote_path)).is_error()

    def capture_smaps(self, pid: int, remote_path: pathlib.PurePosixPath) -> bool:
        quoted_path = shlex.quote(str(remote_path))
        command = (f"cat /proc/{pid}/smaps > {quoted_path} && "
                   f"chmod 0644 {quoted_path}")
        return not self.abd.shell("su", "-c", command).is_error()

    def capture_screenshot(self, remote_path: pathlib.PurePosixPath) -> bool:
        return not self.abd.shell("screencap", "-p", str(remote_path)).is_error()

    def screen_bounds(self) -> ScreenBounds:
        result = self.abd.shell("wm", "size")
        _raise_if_error(result, "screen bounds failed")
        sizes = _SIZE_RE.findall(result.stdout)
        if not sizes:
            raise RuntimeError("screen bounds are malformed")
        width, height = (int(value) for value in sizes[-1])
        if width <= 1 or height <= 1:
            raise RuntimeError("screen bounds are malformed")
        return ScreenBounds(0, 0, width, height)

    def send_file(self, local_path: pathlib.Path, remote_path: pathlib.PurePosixPath) -> CommandResult:
        result = self.abd.run("push", str(local_path), str(remote_path))
        return CommandResult(result.returncode, result.stdout,
                             "" if result.returncode == 0 else result.stderr)

    def recv_file(self, remote_path: pathlib.PurePosixPath, local_path: pathlib.Path) -> CommandResult:
        local_path.parent.mkdir(parents=True, exist_ok=True)
        result = self.abd.run("pull", str(remote_path), str(local_path))
        return CommandResult(result.returncode, result.stdout,
                             "" if result.returncode == 0 else result.stderr)

    def tap(self, point: Point) -> None:
        self._input("tap", str(point.x), str(point.y))

    def double_tap(self, point: Point) -> None:
        self.tap(point)
        self.tap(point)

    def long_tap(self, point: Point) -> None:
        self._input("swipe", str(point.x), str(point.y),
                    str(point.x), str(point.y), "800")

    def swipe(self, start: Point, end: Point, velocity: int) -> None:
        self._swipe(start, end, velocity)

    def drag(self, start: Point, end: Point, velocity: int) -> None:
        duration_ms = _swipe_duration(start, end, velocity)
        self._input("touchscreen", "draganddrop", str(start.x), str(start.y),
                    str(end.x), str(end.y), str(duration_ms))

    def fling(self, start: Point, end: Point, velocity: int, _step_length: int) -> None:
        self._fling(start, end)

    def directional_fling(self, direction: str, velocity: int, _step_length: int) -> None:
        bounds = self.screen_bounds()
        center_x = (bounds.left + bounds.right) // 2
        center_y = (bounds.top + bounds.bottom) // 2
        horizontal_start = bounds.left + (bounds.right - bounds.left) * 3 // 4
        horizontal_end = bounds.left + (bounds.right - bounds.left) // 4
        vertical_start = bounds.top + (bounds.bottom - bounds.top) * 3 // 4
        vertical_end = bounds.top + (bounds.bottom - bounds.top) // 4
        paths = {
            "left": (Point(horizontal_start, center_y), Point(horizontal_end, center_y)),
            "right": (Point(horizontal_end, center_y), Point(horizontal_start, center_y)),
            "up": (Point(center_x, vertical_start), Point(center_x, vertical_end)),
            "down": (Point(center_x, vertical_end), Point(center_x, vertical_start)),
        }
        res = paths.get(direction)
        if res is None:
            raise KeyError(f"Direction {direction} is invalid")
        start, end = res
        self._fling(start, end)

    def input_text(self, point: Point, text: str) -> None:
        self.tap(point)
        self.text(text)

    def text(self, text: str) -> None:
        self._input("text", text.replace(" ", "%s"))

    def send_key(self, key: str) -> None:
        keycodes = {"Home": "KEYCODE_HOME",
                    "Back": "KEYCODE_BACK", "Power": "KEYCODE_POWER"}
        self._input("keyevent", keycodes.get(key) or key)

    def _input(self, *args: str) -> None:
        _raise_if_error(self.abd.shell("input", *args), f"{args[0]} failed")

    def _swipe(self, start: Point, end: Point, velocity: int) -> None:
        duration_ms = _swipe_duration(start, end, velocity)
        self._input("touchscreen", "swipe", str(start.x), str(start.y),
                    str(end.x), str(end.y), str(duration_ms))

    def _fling(self, start: Point, end: Point) -> None:
        self._input("touchscreen", "swipe", str(start.x), str(start.y),
                    str(end.x), str(end.y), str(_FLING_DURATION_MS))
