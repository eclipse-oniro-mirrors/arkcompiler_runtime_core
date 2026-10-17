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

import pathlib
import shlex
import subprocess
import tempfile
import unittest
from unittest import mock

from test.mock.hdc import FakeHdc

from src.device import CommandResult, Point, ThermalZoneHealth
from src.platform.andr0id.abd import Abd
from src.platform.andr0id.abd_device import AbdDevice


class AbdTest(unittest.TestCase):
    def test_shell_quotes_remote_arguments(self) -> None:
        completed = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr="")
        with mock.patch("src.platform.andr0id.abd.subprocess.run", return_value=completed) as run:
            Abd(pathlib.Path("/opt/abd")).shell("input",
                                                "text", "text; not a command")

        command = run.call_args.args[0]
        self.assertEqual(command[:2], ["/opt/abd", "shell"])
        self.assertEqual(shlex.split(command[2]), [
                         "input", "text", "text; not a command"])

    def test_device_translates_lifecycle_files_and_logs(self) -> None:
        abd = FakeHdc(responses={"shell wm size": CommandResult(
            0, "Physical size: 1080x2400\n", "")})
        device = AbdDevice(abd)  # type: ignore[arg-type]

        device.launch_app("com.example", ".MainActivity")
        device.terminate_app("com.example")
        self.assertTrue(device.capture_screenshot(
            pathlib.PurePosixPath("/data/local/tmp/shot.png")))
        self.assertEqual(device.screen_bounds().right, 1080)
        self.assertEqual(abd.calls, [
            ["shell", "am", "start", "-W", "-n", "com.example/.MainActivity"],
            ["shell", "am", "force-stop", "com.example"],
            ["shell", "screencap", "-p", "/data/local/tmp/shot.png"],
            ["shell", "wm", "size"],
        ])

    def test_launch_accepts_advisory_stderr_but_rejects_nonzero_result(self) -> None:
        command = "shell am start -W -n com.example/.MainActivity"
        device = AbdDevice(FakeHdc(responses={
            command: CommandResult(0, "", "Warning: already running\n"),
        }))  # type: ignore[arg-type]

        device.launch_app("com.example", ".MainActivity")

        failed = AbdDevice(FakeHdc(responses={
            command: CommandResult(1, "", "Error: activity missing\n"),
        }))  # type: ignore[arg-type]
        with self.assertRaisesRegex(RuntimeError, "activity missing"):
            failed.launch_app("com.example", ".MainActivity")

    def test_device_health_reads_pixel_services(self) -> None:
        abd = FakeHdc(responses={
            "shell dumpsys battery": CommandResult(0, "  level: 73\n", ""),
            "shell dumpsys thermalservice": CommandResult(
                0,
                "Current temperatures from HAL:\n"
                "  Temperature{mValue=29.000002, mType=1, mName=G3D, mStatus=0}\n"
                "  Temperature{mValue=25.300001, mType=2, mName=battery, mStatus=0}\n"
                "Current cooling devices from HAL:\n",
                "",
            ),
        })
        device = AbdDevice(abd)  # type: ignore[arg-type]

        health = device.device_health()

        self.assertEqual(health.battery_capacity_pct, 73)
        self.assertEqual(health.thermal_zones, [
            ThermalZoneHealth("G3D", 29000),
            ThermalZoneHealth("battery", 25300),
        ])

    def test_wait_boot_completed_polls_boot_property(self) -> None:
        abd = FakeHdc(responses={
            "shell getprop sys.boot_completed": CommandResult(0, "1\n", ""),
        })

        device = AbdDevice(abd)  # type: ignore[arg-type]
        device.wait_boot_completed(1, poll_interval_seconds=0)

        self.assertEqual(abd.calls, [
            ["shell", "getprop", "sys.boot_completed"],
        ])

    def test_capture_smaps_uses_root_shell_and_readable_mode(self) -> None:
        abd = FakeHdc()
        remote_path = pathlib.PurePosixPath("/data/local/tmp/out file.smaps")

        device = AbdDevice(abd)  # type: ignore[arg-type]
        self.assertTrue(device.capture_smaps(123, remote_path))

        quoted_path = shlex.quote(str(remote_path))
        self.assertEqual(abd.calls, [[
            "shell", "su", "-c",
            f"cat /proc/123/smaps > {quoted_path} && chmod 0644 {quoted_path}",
        ]])

    def test_file_transfer_ignores_successful_abd_progress_stderr(self) -> None:
        remote_path = pathlib.PurePosixPath("/data/local/tmp/artifact.log")
        with tempfile.TemporaryDirectory() as directory:
            local_path = pathlib.Path(directory).joinpath("artifact.log")
            abd = FakeHdc(responses={
                f"pull {remote_path} {local_path}": CommandResult(
                    0, "", "artifact.log: 1 file pulled\n"),
            })
            device = AbdDevice(abd)  # type: ignore[arg-type]

            result = device.recv_file(remote_path, local_path)

        self.assertFalse(result.is_error())
        self.assertEqual(result.stderr, "")

    def test_device_translates_inputs(self) -> None:
        abd = FakeHdc(responses={"shell wm size": CommandResult(
            0, "Physical size: 100x200\n", "")})
        device = AbdDevice(abd)  # type: ignore[arg-type]

        device.double_tap(Point(10, 20))
        device.long_tap(Point(10, 20))
        device.swipe(Point(0, 0), Point(100, 0), 1000)
        device.swipe(Point(0, 0), Point(1000, 0), 200)
        device.drag(Point(0, 0), Point(100, 0), 1000)
        device.fling(Point(0, 0), Point(100, 0), 1000, 20)
        device.directional_fling("up", 1000, 20)
        device.directional_fling("left", 1000, 20)
        device.input_text(Point(5, 6), "hello world")
        device.send_key("Home")

        self.assertEqual(abd.calls, [
            ["shell", "input", "tap", "10", "20"],
            ["shell", "input", "tap", "10", "20"],
            ["shell", "input", "swipe", "10", "20", "10", "20", "800"],
            ["shell", "input", "touchscreen", "swipe", "0", "0", "100", "0", "150"],
            ["shell", "input", "touchscreen", "swipe", "0", "0", "1000", "0", "500"],
            ["shell", "input", "touchscreen",
                "draganddrop", "0", "0", "100", "0", "150"],
            ["shell", "input", "touchscreen", "swipe", "0", "0", "100", "0", "120"],
            ["shell", "wm", "size"],
            ["shell", "input", "touchscreen", "swipe",
                "50", "150", "50", "50", "120"],
            ["shell", "wm", "size"],
            ["shell", "input", "touchscreen", "swipe",
                "75", "100", "25", "100", "120"],
            ["shell", "input", "tap", "5", "6"],
            ["shell", "input", "text", "hello%sworld"],
            ["shell", "input", "keyevent", "KEYCODE_HOME"],
        ])


if __name__ == "__main__":
    unittest.main()
