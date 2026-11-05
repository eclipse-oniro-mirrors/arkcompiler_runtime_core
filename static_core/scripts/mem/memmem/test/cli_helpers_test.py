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
import pathlib
import tempfile
import unittest
from unittest import mock

from src.cli_helpers import (execute_iterations, load_config, load_device,
                             load_flow, view_destination)

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
            with mock.patch("src.cli_helpers.lib.get_hdc_device") as get_device:
                load_device(env_path, "hdc")
            get_device.assert_called_once()
            with self.assertRaisesRegex(ValueError, "ABD_PATH"):
                load_device(env_path, "abd")

    def test_execute_iterations_writes_one_flow_and_exact_destinations(self) -> None:
        flow = lib.flow([])
        device = mock.create_autospec(lib.Device, instance=True)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory) / "out"
            with mock.patch("src.cli_helpers.lib.execute_flow",
                            side_effect=lambda *_args, **_kwargs: _args[2]) as execute:
                paths = execute_iterations(flow, device, root, 2, reboot=True,
                                           device_log=False)

            self.assertEqual(paths, [root / "iteration_0" / "evidence",
                                     root / "iteration_1" / "evidence"])
            self.assertEqual([call.args[2]
                             for call in execute.call_args_list], paths)
            self.assertTrue(root.joinpath("flow.json").is_file())
            self.assertEqual(json.loads(root.joinpath("flow.json").read_text()),
                             flow.model_dump())

    def test_execute_iterations_rejects_existing_flow_before_execution(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            root.joinpath("flow.json").write_text("{}", encoding="utf-8")
            with mock.patch("src.cli_helpers.lib.execute_flow") as execute:
                with self.assertRaises(FileExistsError):
                    execute_iterations(lib.flow([]), mock.create_autospec(
                        lib.Device, instance=True), root, 1, reboot=False,
                        device_log=True)
            execute.assert_not_called()

    def test_execute_iterations_preflights_before_writing_provenance(self) -> None:
        device = mock.create_autospec(lib.Device, instance=True)
        device.is_command_supported.return_value = False
        flow = lib.flow([lib.app_flow(
            "App", "com.example", "EntryAbility", False, [lib.text("text")])])
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).joinpath("out")

            with self.assertRaisesRegex(RuntimeError, "does not support command"):
                execute_iterations(flow, device, root, 1, reboot=False,
                                   device_log=True)

            self.assertFalse(root.exists())

    def test_execute_iterations_retains_flow_and_successful_prefix_on_failure(self) -> None:
        flow = lib.flow([])
        device = mock.create_autospec(lib.Device, instance=True)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).joinpath("out")
            with mock.patch("src.cli_helpers.lib.execute_flow", side_effect=[
                    root / "iteration_0" / "evidence", RuntimeError("failed"),
            ]):
                with self.assertRaisesRegex(RuntimeError, "failed"):
                    execute_iterations(flow, device, root, 2, reboot=False,
                                       device_log=True)

            self.assertTrue(root.joinpath("flow.json").is_file())

    def test_view_destination_uses_timestamped_view_directory(self) -> None:
        self.assertEqual(view_destination(pathlib.Path("out"), "20260101_010203_000004"),
                         pathlib.Path.cwd() / "out" / "view" /
                         "view-20260101_010203_000004")

    def test_run_collects_all_evidence_before_rendering(self) -> None:
        evidence = [pathlib.Path("/tmp/evidence-0"),
                    pathlib.Path("/tmp/evidence-1")]
        flow = lib.flow([])
        with mock.patch("sys.argv", [
                "run.py", "--flow", "flow.json", "--out-dir", "out", "--repeats", "2",
        ]), mock.patch("run.load_flow", return_value=flow), mock.patch(
                "run.load_device"), mock.patch("run.execute_iterations",
                                               return_value=evidence) as collect, mock.patch(
                "run.lib.create_view") as create:
            self.assertEqual(run.main(), 0)

        self.assertEqual(collect.call_count, 1)
        self.assertEqual([call.args[0] for call in create.call_args_list],
                         [[evidence[0]], [evidence[1]], evidence])

    def test_run_stops_rendering_after_first_view_failure_and_resets_logger(self) -> None:
        flow = lib.flow([])
        with mock.patch("sys.argv", [
                "run.py", "--flow", "flow.json", "--out-dir", "out",
        ]), mock.patch("run.load_flow", return_value=flow), mock.patch(
                "run.load_device"), mock.patch("run.execute_iterations",
                                               return_value=[pathlib.Path("evidence")]), \
                mock.patch("run.lib.create_view", side_effect=RuntimeError("render")) as create, \
                mock.patch("run.lib.reset_logger") as reset:
            self.assertEqual(run.main(), 1)

        create.assert_called_once()
        reset.assert_called_once()


if __name__ == "__main__":
    unittest.main()
