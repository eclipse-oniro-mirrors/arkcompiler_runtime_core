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
import re
import tempfile
import typing
import unittest
from unittest import mock

from test.mock.device import FakeDevice

import pydantic

from src.device import ScreenBounds
from src.evidence import validate_evidence
from src.metadata import EvidenceMetadata
from src.store import EvidenceStore
from src.log import configure_logger, get_logger, reset_logger

import lib


class LibTest(unittest.TestCase):
    def test_execution_writes_canonical_provenance_before_preparation(self) -> None:
        flow = lib.preprocess_flow(lib.UnprocessedFlow.model_validate({
            "$desc": "expanded", "flow": [{
                "label": "app", "bundle": "com.example", "ability": "EntryAbility",
                "terminate": False, "commands": [{"macro": "repeat", "payload": {
                    "iter_var": "i", "n_iter": 2,
                    "commands": [{"action": "snapshot", "payload": "s_{i}"}],
                }}],
            }],
        }))
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory) / "evidence"
            store = EvidenceStore(destination)

            class FailingPreparation(FakeDevice):
                def reboot(self) -> None:
                    # Provenance is already readable before any reboot work.
                    validate_evidence(store)
                    raise RuntimeError("preparation stopped")

            with self.assertRaisesRegex(RuntimeError, "preparation stopped"):
                lib.execute_flow(flow, FailingPreparation(), destination,
                                 reboot=True, device_log=True)
            parsed = validate_evidence(store)
            self.assertEqual(parsed.flow, flow)
            self.assertEqual(parsed.metadata, EvidenceMetadata(
                flow="flow.json", reboot=True, device_log=True))
            self.assertFalse(destination.parent.joinpath("flow.json").exists())

    def test_builder_rejects_coercible_builtin_value(self) -> None:
        with self.assertRaises(pydantic.ValidationError):
            lib.app_flow("app", "com.example", "EntryAbility",
                         typing.cast(bool, 1), [])

    def test_execute_flow_returns_evidence_destination(self) -> None:
        flow = lib.flow([lib.app_flow("app", "com.example", "EntryAbility",
                                      False, [])])
        device = FakeDevice(screen=ScreenBounds(0, 0, 100, 200))
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")

            result = lib.execute_flow(
                flow, device, destination, device_log=False)

            self.assertEqual(result, destination)
            self.assertTrue(result.joinpath("apps.json").is_file())
            self.assertTrue(result.joinpath(
                "snapshots", "metadata.json").is_file())
            self.assertTrue(result.joinpath(
                "screenshots", "metadata.json").is_file())
            self.assertFalse(result.joinpath("summary.csv").exists())

    def test_execute_flow_rejects_unsupported_command_before_destination(self) -> None:
        flow = lib.flow([lib.app_flow("app", "com.example", "EntryAbility",
                                      False, [lib.text("text")])])
        device = FakeDevice(unsupported_commands=[flow.flow[0].commands[0]])
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("evidence")

            with self.assertRaisesRegex(RuntimeError, "does not support command"):
                lib.execute_flow(flow, device, destination, device_log=False)

            self.assertFalse(destination.exists())

    def test_create_view_rejects_duplicate_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            evidence = pathlib.Path(directory).joinpath("evidence")

            with self.assertRaisesRegex(ValueError, "duplicate evidence"):
                lib.create_view([evidence, evidence], pathlib.Path(
                    directory).joinpath("view"))

    def test_create_view_rejects_invalid_smaps_filters_before_reading_evidence(self) -> None:
        class PseudoPattern:
            pattern = ".*"
            flags = 0

            def match(self, _value: str) -> bool:
                return True

        invalid_filters: list[object] = [
            None, ".*", re.compile(b".*"), PseudoPattern()]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for smaps_filter in invalid_filters:
                destination = root / str(len(str(smaps_filter)))
                with self.assertRaisesRegex(TypeError, "compiled string pattern"):
                    lib.create_view([root / "missing"], destination,
                                    smaps_filter=typing.cast(re.Pattern[str], smaps_filter))
                self.assertFalse(destination.exists())

    def test_create_view_keeps_new_partial_destination_on_write_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("view")
            evidence = lib.execute_flow(lib.flow([]), FakeDevice(),
                                        pathlib.Path(directory) / "evidence", device_log=False)

            def fail_after_creation(*args: object, **kwargs: object) -> None:
                destination.mkdir()
                raise RuntimeError("write failed")

            with mock.patch("lib.write_view", side_effect=fail_after_creation):
                with self.assertRaisesRegex(RuntimeError, "write failed"):
                    lib.create_view([evidence], destination)

            self.assertTrue(destination.is_dir())

    def test_execute_flow_preserves_caller_logger(self) -> None:
        flow = lib.flow([])
        device = FakeDevice(screen=ScreenBounds(0, 0, 100, 200))
        configure_logger("info")
        logger = get_logger()
        try:
            with tempfile.TemporaryDirectory() as directory:
                lib.execute_flow(flow, device, pathlib.Path(directory).joinpath("evidence"),
                                 device_log=False)
            self.assertIs(get_logger(), logger)
        finally:
            reset_logger()


if __name__ == "__main__":
    unittest.main()
