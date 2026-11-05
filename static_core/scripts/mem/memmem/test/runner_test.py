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

import os
import pathlib
import subprocess
import tempfile
import typing
import unittest

from test.mock.device import FakeDevice

from src.commands import PendingArtifact
from src.device import CommandResult, ScreenBounds
from src.evidence import validate_evidence
from src.runner import (BenchmarkOptions, _receive_pending_artifact,
                        create_evidence_store, run_benchmark)
from src.schema import Flow


class RunnerTest(unittest.TestCase):
    def test_successful_empty_flow_writes_valid_evidence_lifecycle(self) -> None:
        flow = Flow.model_validate({"flow": []})
        device = FakeDevice(screen=ScreenBounds(0, 0, 100, 200))
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            options = BenchmarkOptions(destination, device_log=False)
            store = create_evidence_store(options)

            run_benchmark(flow, options, store, device)

            self.assertTrue(store.local_app_metadata_path().is_file())
            self.assertTrue(store.local_artifact_metadata_path(
                store.local_snapshots_dir()).is_file())
            self.assertTrue(store.local_artifact_metadata_path(
                store.local_screenshots_dir()).is_file())
            validate_evidence(store)
            self.assertFalse(device.dirs)

    def test_existing_destination_is_not_changed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            destination.mkdir()
            sentinel = destination.joinpath("sentinel")
            sentinel.write_text("keep", encoding="utf-8")
            options = BenchmarkOptions(destination, device_log=False)

            with self.assertRaises(FileExistsError):
                run_benchmark(Flow(flow=[]), options,
                              create_evidence_store(options), FakeDevice())

            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_failure_receives_successful_prefix_restores_cwd_and_cleans_remote(self) -> None:
        class TrackingDevice(FakeDevice):
            def __init__(self) -> None:
                super().__init__(processes={"com.example": 1, "com.failed": 2})
                self.received: list[pathlib.PurePosixPath] = []

            def capture_smaps(self, pid: int, path: pathlib.PurePosixPath) -> bool:
                return pid != 2 and super().capture_smaps(pid, path)

            def recv_file(self, remote_path: pathlib.PurePosixPath,
                          local_path: pathlib.Path) -> CommandResult:
                self.received.append(remote_path)
                return super().recv_file(remote_path, local_path)

        flow = Flow.model_validate({"flow": [
            {"label": "App", "bundle": "com.example", "ability": "EntryAbility",
             "terminate": False, "commands": []},
            {"label": "Failed", "bundle": "com.failed", "ability": "EntryAbility",
             "terminate": False, "commands": [{"action": "snapshot", "payload": "start"}]},
        ]})
        device = TrackingDevice()
        original_cwd = pathlib.Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            options = BenchmarkOptions(destination, device_log=False)

            with self.assertRaisesRegex(RuntimeError, "failed to capture smaps"):
                run_benchmark(
                    flow, options, create_evidence_store(options), device)

            self.assertEqual(pathlib.Path.cwd(), original_cwd)
            self.assertEqual(len(device.received), 1)
            self.assertFalse(device.dirs)
            self.assertTrue(destination.joinpath(
                "snapshots", "start", "App.smaps").is_file())

    def test_success_validates_structure_without_rendering_a_view(self) -> None:
        flow = Flow(flow=[])
        device = FakeDevice()
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            options = BenchmarkOptions(destination, device_log=False)
            store = create_evidence_store(options)
            run_benchmark(flow, options, store, device)

            validate_evidence(store)

    def test_explicit_termination_excludes_only_its_label_when_pids_are_shared(self) -> None:
        flow = Flow.model_validate({"flow": [
            {"label": "First", "bundle": "com.first", "ability": "EntryAbility",
             "terminate": True, "commands": []},
            {"label": "Second", "bundle": "com.second", "ability": "EntryAbility",
             "terminate": False, "commands": [{"action": "snapshot", "payload": "after"}]},
        ]})
        device = FakeDevice(processes={"com.first": 7, "com.second": 7})
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            options = BenchmarkOptions(destination, device_log=False)
            run_benchmark(
                flow, options, create_evidence_store(options), device)

            metadata = destination.joinpath("snapshots", "metadata.json").read_text(
                encoding="utf-8")
            self.assertIn("after/Second.smaps", metadata)
            self.assertNotIn("after/First.smaps", metadata)

    def test_early_log_exit_preserves_partial_log_and_cleans_remote(self) -> None:
        class ExitingLogDevice(FakeDevice):
            def start_log_capture(
                self, path: pathlib.PurePosixPath
            ) -> subprocess.Popen[typing.Any]:
                process = super().start_log_capture(path)
                process.terminate()
                process.wait()
                return process

        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")
            options = BenchmarkOptions(destination)
            device = ExitingLogDevice()
            with self.assertRaisesRegex(RuntimeError, "exited before requested shutdown"):
                run_benchmark(Flow(flow=[]), options,
                              create_evidence_store(options), device)

            self.assertTrue(destination.joinpath("logs", "log.log").is_file())
            self.assertFalse(device.dirs)

    def test_receive_uses_cwd_relative_local_path(self) -> None:
        class ReceivingDevice(FakeDevice):
            def __init__(self) -> None:
                super().__init__()
                self.local_path: pathlib.Path | None = None

            def recv_file(self, remote_path: pathlib.PurePosixPath,
                          local_path: pathlib.Path) -> CommandResult:
                self.local_path = local_path
                self.files[remote_path] = "smaps"
                return super().recv_file(remote_path, local_path)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            device = ReceivingDevice()
            pending = PendingArtifact(pathlib.PurePosixPath("/remote"), root,
                                      ["snapshots", "start", "App.smaps"])
            old_cwd = pathlib.Path.cwd()
            try:
                os.chdir(root)
                _receive_pending_artifact(device, pending)
            finally:
                os.chdir(old_cwd)

            self.assertIsNotNone(device.local_path)
            assert device.local_path is not None
            self.assertFalse(device.local_path.is_absolute())

    def test_failure_phases_preserve_primary_error_and_restore_cwd(self) -> None:
        snapshot_flow = Flow.model_validate({"flow": [{
            "label": "App", "bundle": "com.example", "ability": "EntryAbility",
            "terminate": False, "commands": [{"action": "snapshot", "payload": "start"}],
        }]})
        cases = [
            ("remote", snapshot_flow, False, FakeDevice(fail_make_dir=True)),
            ("stream", snapshot_flow, True, FakeDevice(fail_device_log=True)),
            ("capture", snapshot_flow, False, FakeDevice(fail_capture_smaps=True)),
            ("receive", snapshot_flow, False, FakeDevice(fail_recv_file=True)),
            ("cleanup", Flow(flow=[]), False, FakeDevice(fail_remove_dir=True)),
        ]
        original_cwd = pathlib.Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            for name, flow, device_log, device in cases:
                destination = pathlib.Path(directory).joinpath(name)
                options = BenchmarkOptions(destination, device_log=device_log)
                with self.assertRaises(RuntimeError):
                    run_benchmark(
                        flow, options, create_evidence_store(options), device)
                self.assertEqual(pathlib.Path.cwd(), original_cwd)
                self.assertTrue(destination.exists())

    def test_receiving_failure_does_not_skip_later_artifacts(self) -> None:
        class TrackingDevice(FakeDevice):
            def __init__(self) -> None:
                super().__init__(processes={"com.first": 1, "com.second": 2},
                                 fail_recv_names={"First.smaps"})
                self.received: list[pathlib.PurePosixPath] = []

            def recv_file(self, remote_path: pathlib.PurePosixPath,
                          local_path: pathlib.Path) -> CommandResult:
                self.received.append(remote_path)
                return super().recv_file(remote_path, local_path)

        flow = Flow.model_validate({"flow": [
            {"label": "First", "bundle": "com.first", "ability": "EntryAbility",
             "terminate": False, "commands": []},
            {"label": "Second", "bundle": "com.second", "ability": "EntryAbility",
             "terminate": False, "commands": [{"action": "snapshot", "payload": "start"}]},
        ]})
        with tempfile.TemporaryDirectory() as directory:
            options = BenchmarkOptions(pathlib.Path(directory).joinpath("evidence"),
                                       device_log=False)
            device = TrackingDevice()
            with self.assertRaisesRegex(RuntimeError, "missing remote file"):
                run_benchmark(
                    flow, options, create_evidence_store(options), device)

            self.assertEqual([path.name for path in device.received],
                             ["First.smaps", "Second.smaps"])

    def test_enabled_logging_receives_log_log_and_disabled_logging_skips_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("enabled")
            options = BenchmarkOptions(destination)
            device = FakeDevice()
            run_benchmark(Flow(flow=[]), options,
                          create_evidence_store(options), device)
            self.assertEqual(device.log_paths[-1].name, "log.log")
            self.assertTrue(destination.joinpath("logs", "log.log").is_file())

            disabled_destination = pathlib.Path(directory).joinpath("disabled")
            disabled_options = BenchmarkOptions(
                disabled_destination, device_log=False)
            disabled_device = FakeDevice()
            run_benchmark(Flow(flow=[]), disabled_options,
                          create_evidence_store(disabled_options), disabled_device)
            self.assertFalse(disabled_device.log_paths)
            self.assertFalse(disabled_destination.joinpath("logs").exists())


if __name__ == "__main__":
    unittest.main()
