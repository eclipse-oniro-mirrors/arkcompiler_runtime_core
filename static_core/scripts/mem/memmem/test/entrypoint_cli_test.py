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

import contextlib
import io
import pathlib
import tempfile
import unittest
from unittest import mock

from test.mock.device import FakeDevice
from test.operation_hooks_test import logging_hook, trace

from src.store import ViewStore
from src.view import read_view

import create_view
import execute_flow
import lib


class EntrypointCliTest(unittest.TestCase):
    def test_execute_flow_defaults_and_no_view_filter(self) -> None:
        args = execute_flow.build_parser().parse_args(["--flow", "flow.json"])
        self.assertEqual(args.repeats, 1)
        self.assertTrue(args.device_log)
        self.assertFalse(args.reboot)
        self.assertFalse(hasattr(args, "smaps_filter"))

    def test_create_view_preserves_source_order_and_default_filter(self) -> None:
        args = create_view.build_parser().parse_args([
            "first", "second", "--out-dir", "out",
        ])
        self.assertEqual(args.dirs, [pathlib.Path(
            "first"), pathlib.Path("second")])
        self.assertEqual(args.smaps_filter.pattern, ".*")
        self.assertTrue(args.plots)

    def test_parsers_reject_abbreviations_and_invalid_regex(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                execute_flow.build_parser().parse_args(["--fl", "flow.json"])
            with self.assertRaises(SystemExit):
                create_view.build_parser().parse_args([
                    "source", "--out-dir", "out", "--smaps-filter", "["])

    def test_execute_flow_main_uses_iteration_zero_without_creating_views(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            flow_path = root / "input.json"
            flow_path.write_text(lib.flow([]).model_dump_json())
            hook = logging_hook(root)
            with mock.patch("execute_flow.load_device", return_value=FakeDevice()):
                self.assertEqual(execute_flow.main([
                    "--flow", str(flow_path), "--out-dir", str(root), "--no-device-log",
                    "--pre-execute-flow-hook", str(hook),
                    "--post-execute-flow-hook", str(hook),
                ]), 0)

            entries = trace(root)
            self.assertEqual([entry["context"]["event"] for entry in entries],
                             ["pre_execute_flow", "post_execute_flow"])
            self.assertEqual(entries[0]["context"]["dst"], str(
                root / "iteration_0/evidence"))
            self.assertTrue(
                (root / "iteration_0/evidence/metadata.json").is_file())
            self.assertFalse((root / "iteration_1").exists())
            self.assertFalse((root / "iteration_0/view").exists())
            self.assertFalse((root / "view").exists())

    def test_create_view_main_preserves_sources_and_accepts_existing_parent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            sources = [lib.execute_flow(lib.flow([]), FakeDevice(), root / name,
                                        device_log=False) for name in ("first", "second")]
            hook = logging_hook(root)
            self.assertEqual(create_view.main([
                *map(str, sources), "--out-dir", str(root), "--no-plots",
                "--pre-create-view-hook", str(
                    hook), "--post-create-view-hook", str(hook),
            ]), 0)
            destinations = list((root / "view").iterdir())
            self.assertEqual(len(destinations), 1)
            self.assertRegex(destinations[0].name, r"^view-\d{8}_\d{6}_\d{6}$")
            view = read_view(ViewStore(destinations[0]))
            self.assertEqual(view.metadata.dirs, sources)
            self.assertFalse(view.metadata.plots)
            entries = trace(root)
            self.assertEqual([entry["context"]["event"] for entry in entries],
                             ["pre_create_view", "post_create_view"])
            for entry in entries:
                self.assertEqual(entry["context"]["dirs"],
                                 list(map(str, sources)))
                self.assertEqual(entry["context"]["dst"], str(destinations[0]))

    def test_entrypoints_reset_logger_after_operation_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            log = root / "memmem.log"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                self.assertEqual(create_view.main([
                    str(root / "missing"), "--out-dir", str(root / "out"),
                    "--memmem-log-file", str(log),
                ]), 1)
                lib.get_logger().err("after reset")
            lib.reset_logger()
            self.assertIn("missing evidence metadata", stderr.getvalue())
            self.assertIn("error: after reset", stderr.getvalue())
            self.assertNotIn("after reset", log.read_text())
            self.assertFalse((root / "out").exists())


if __name__ == "__main__":
    unittest.main()
