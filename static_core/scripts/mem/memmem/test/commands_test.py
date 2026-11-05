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
import tempfile
import unittest

from test.mock.device import FakeDevice

from src.commands import ExecutionContext, execute_command
from src.device import ScreenBounds
from src.metadata import AppMetadata
from src.store import EvidenceStore
from src.schema import Flow


class CommandsTest(unittest.TestCase):
    def test_snapshot_declaration_survives_failed_capture(self) -> None:
        class PartiallyFailingDevice(FakeDevice):
            def capture_smaps(self, pid: int, path: pathlib.PurePosixPath) -> bool:
                if pid == 2:
                    return False
                return super().capture_smaps(pid, path)

        with tempfile.TemporaryDirectory() as directory:
            store = EvidenceStore(
                pathlib.Path(directory), pathlib.PurePosixPath("/remote"))
            device = PartiallyFailingDevice(processes={"com.example": 1,
                                                       "com.failed": 2})
            active = [
                AppMetadata(pid=1, label="App", bundle="com.example",
                            ability="EntryAbility"),
                AppMetadata(pid=2, label="Failed", bundle="com.failed",
                            ability="EntryAbility"),
            ]
            context = ExecutionContext(
                device=device,
                store=store,
                flow=Flow(flow=[]),
                launched_apps=active,
                active_apps=active.copy(),
                pending_artifacts=[],
                artifact_metadata={store.local_snapshots_dir(): [],
                                   store.local_screenshots_dir(): []},
                child_processes=[],
                screen_bounds=ScreenBounds(0, 0, 100, 200),
            )
            command = Flow.model_validate({"flow": [{
                "label": "app", "bundle": "com.example",
                "ability": "EntryAbility", "terminate": False,
                "commands": [{"action": "snapshot", "payload": "start"}],
            }]}).flow[0].commands[0]

            with self.assertRaisesRegex(RuntimeError, "PID 2"):
                execute_command(command, context)

            self.assertEqual(context.artifact_metadata[store.local_snapshots_dir()][0].files,
                             ["start/App.smaps", "start/Failed.smaps"])
            self.assertEqual(context.pending_artifacts[0].artifact,
                             ["start", "App.smaps"])

    def test_snapshot_missing_pid_keeps_declaration_and_successful_prefix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = EvidenceStore(pathlib.Path(directory),
                                  pathlib.PurePosixPath("/remote"))
            device = FakeDevice(processes={"com.example": 1})
            active = [
                AppMetadata(pid=1, label="App", bundle="com.example",
                            ability="EntryAbility"),
                AppMetadata(pid=2, label="Gone", bundle="com.gone",
                            ability="EntryAbility"),
            ]
            context = ExecutionContext(
                device=device, store=store, flow=Flow(flow=[]), launched_apps=active,
                active_apps=active.copy(), pending_artifacts=[],
                artifact_metadata={store.local_snapshots_dir(): [],
                                   store.local_screenshots_dir(): []},
                child_processes=[], screen_bounds=ScreenBounds(0, 0, 100, 200))
            command = Flow.model_validate({"flow": [{
                "label": "app", "bundle": "com.example", "ability": "EntryAbility",
                "terminate": False, "commands": [{"action": "snapshot", "payload": "start"}],
            }]}).flow[0].commands[0]

            with self.assertRaisesRegex(RuntimeError, "label=Gone pid=2"):
                execute_command(command, context)

            self.assertEqual(context.artifact_metadata[store.local_snapshots_dir()][0].files,
                             ["start/App.smaps", "start/Gone.smaps"])
            self.assertEqual([item.artifact for item in context.pending_artifacts],
                             [["start", "App.smaps"]])


if __name__ == "__main__":
    unittest.main()
