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

import json
import contextlib
import io
import pathlib
import tempfile
import unittest
from unittest import mock

from test.mock.device import FakeDevice
from test.operation_hooks_test import logging_hook, trace

from src.cli_helpers import (execute_iterations, load_config, load_device,
                             load_flow, view_destination)
from src.evidence import validate_evidence
from src.platform.ohos.hdc_device import HdcDevice
from src.store import EvidenceStore

import run
import lib


class CliHelpersTest(unittest.TestCase):
    def test_load_flow_canonicalizes_macros_and_preserves_description(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory).joinpath("flow.json")
            path.write_text(json.dumps({
                "$desc": "generated",
                "flow": [{
                    "label": "App", "bundle": "com.example",
                    "ability": "EntryAbility", "terminate": False,
                    "commands": [{
                        "macro": "repeat",
                        "payload": {"iter_var": "i", "n_iter": 2,
                                    "commands": [{"action": "snapshot",
                                                  "payload": "shot_{i}"}]},
                    }],
                }],
            }), encoding="utf-8")

            flow = load_flow(path)

        self.assertEqual(flow.desc, "generated")
        self.assertEqual([command.payload for command in flow.flow[0].commands],
                         ["shot_0", "shot_1"])

    def test_config_and_device_require_only_selected_backend(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            env_path = pathlib.Path(directory).joinpath(".env")
            env_path.write_text("HDC_PATH=/bin/false\n", encoding="utf-8")

            config = load_config(env_path)
            self.assertEqual(config.hdc_path, pathlib.Path("/bin/false"))
            self.assertIsNone(config.abd_path)
            self.assertIsInstance(load_device(env_path, "hdc"), HdcDevice)
            with self.assertRaisesRegex(ValueError, "ABD_PATH"):
                load_device(env_path, "abd")

    def test_execute_iterations_uses_exact_destinations_without_root_flow(self) -> None:
        flow = lib.flow([])
        device = FakeDevice()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory) / "out"
            paths = execute_iterations(flow, device, root, 2, reboot=False,
                                       device_log=False, pre_hooks=[], post_hooks=[])

            self.assertEqual(paths, [root / "iteration_0" / "evidence",
                                     root / "iteration_1" / "evidence"])
            for path in paths:
                evidence = validate_evidence(EvidenceStore(path))
                self.assertEqual(evidence.flow, flow)
                self.assertFalse(evidence.metadata.device_log)
            self.assertFalse(root.joinpath("flow.json").exists())

    def test_execute_iterations_preserves_unrelated_parent_flow(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            root.joinpath("flow.json").write_text("{}", encoding="utf-8")
            paths = execute_iterations(lib.flow([]), FakeDevice(), root, 1,
                                       reboot=False, device_log=False, pre_hooks=[], post_hooks=[])
            self.assertEqual(paths, [root / "iteration_0" / "evidence"])
            validate_evidence(EvidenceStore(paths[0]))
            self.assertEqual(root.joinpath("flow.json").read_text(), "{}")

    def test_execute_iterations_leaves_parent_without_evidence_for_unsupported_command(self) -> None:
        flow = lib.flow([lib.app_flow(
            "App", "com.example", "EntryAbility", False, [lib.text("text")])])
        device = FakeDevice(unsupported_commands=flow.flow[0].commands)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).joinpath("out")

            with self.assertRaisesRegex(RuntimeError, "does not support command"):
                execute_iterations(flow, device, root, 1, reboot=False,
                                   device_log=True, pre_hooks=[], post_hooks=[])

            self.assertTrue(root.is_dir())
            self.assertFalse((root / "iteration_0").exists())

    def test_execute_iterations_failure_does_not_write_root_flow(self) -> None:
        flow = lib.flow([])

        class FailSecondIteration(FakeDevice):
            def disable_screen_timeout(self) -> None:
                if self.screen_timeout_disabled:
                    raise RuntimeError("second iteration failed")
                super().disable_screen_timeout()

        device = FailSecondIteration()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).joinpath("out")
            with self.assertRaisesRegex(RuntimeError, "second iteration failed"):
                execute_iterations(flow, device, root, 3, reboot=False,
                                   device_log=False, pre_hooks=[], post_hooks=[])

            self.assertFalse(root.joinpath("flow.json").exists())
            validate_evidence(EvidenceStore(root / "iteration_0" / "evidence"))
            self.assertTrue(
                (root / "iteration_1/evidence/metadata.json").is_file())
            self.assertFalse((root / "iteration_2").exists())

    def test_view_destination_uses_timestamped_view_directory(self) -> None:
        self.assertEqual(view_destination(pathlib.Path("out"), "20260101_010203_000004"),
                         pathlib.Path.cwd() / "out" / "view" /
                         "view-20260101_010203_000004")

    def test_run_collects_all_evidence_before_rendering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            flow_path = root / "input.json"
            flow_path.write_text(lib.flow([]).model_dump_json())
            hook = logging_hook(root)
            with mock.patch("run.load_device", return_value=FakeDevice()):
                self.assertEqual(run.main([
                    "--flow", str(flow_path), "--out-dir", str(root / "out"),
                    "--repeats", "2", "--no-device-log", "--no-plots",
                    "--pre-execute-flow-hook", str(
                        hook), "--post-execute-flow-hook", str(hook),
                    "--pre-create-view-hook", str(
                        hook), "--post-create-view-hook", str(hook),
                ]), 0)
            entries = trace(root)
            self.assertEqual([entry["context"]["event"] for entry in entries],
                             ["pre_execute_flow", "post_execute_flow"] * 2 +
                             ["pre_create_view", "post_create_view"] * 3)
            evidence = [
                str(root / "out" / f"iteration_{i}" / "evidence") for i in range(2)]
            self.assertEqual([entries[i]["context"]["dirs"] for i in (4, 6, 8)],
                             [[evidence[0]], [evidence[1]], evidence])
            self.assertEqual(len({pathlib.Path(entries[i]["context"]["dst"]).name
                                  for i in (4, 6, 8)}), 1)

    def test_run_stops_rendering_after_first_view_failure_and_resets_logger(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            flow_path = root / "input.json"
            flow_path.write_text(lib.flow([]).model_dump_json())
            hook = logging_hook(root)
            # Make the view destination collide after collection, before rendering.
            script = hook.read_text().replace(
                "sys.exit(", "pathlib.Path(context['dst']).mkdir(parents=True)\nsys.exit(")
            hook.write_text(script)
            stderr = io.StringIO()
            with mock.patch("run.load_device", return_value=FakeDevice()), \
                    contextlib.redirect_stderr(stderr):
                self.assertEqual(run.main([
                    "--flow", str(flow_path), "--out-dir", str(root / "out"),
                    "--repeats", "2", "--no-device-log", "--no-plots",
                    "--pre-create-view-hook", str(hook),
                    "--memmem-log-file", str(root / "log"),
                ]), 1)
                lib.get_logger().err("after reset")
            lib.reset_logger()
            self.assertEqual(len(trace(root)), 1)
            self.assertIn("File exists", stderr.getvalue())
            self.assertIn("error: after reset", stderr.getvalue())
            self.assertNotIn("after reset", (root / "log").read_text())
            self.assertFalse((root / "out/iteration_1/view").exists())
            self.assertFalse((root / "out/view").exists())


if __name__ == "__main__":
    unittest.main()
