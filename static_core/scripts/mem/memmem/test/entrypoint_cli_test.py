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

import create_view
import execute_flow
from create_view import build_parser as create_view_parser
from execute_flow import build_parser as execute_flow_parser


class EntrypointCliTest(unittest.TestCase):
    def test_execute_flow_defaults_and_no_view_filter(self) -> None:
        args = execute_flow_parser().parse_args(["--flow", "flow.json"])
        self.assertEqual(args.repeats, 1)
        self.assertTrue(args.device_log)
        self.assertFalse(args.reboot)
        self.assertFalse(hasattr(args, "smaps_filter"))

    def test_create_view_preserves_source_order_and_default_filter(self) -> None:
        args = create_view_parser().parse_args([
            "first", "second", "--out-dir", "out",
        ])
        self.assertEqual(args.dirs, [pathlib.Path(
            "first"), pathlib.Path("second")])
        self.assertEqual(args.smaps_filter.pattern, ".*")

    def test_parsers_reject_abbreviations_and_invalid_regex(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                execute_flow_parser().parse_args(["--fl", "flow.json"])
            with self.assertRaises(SystemExit):
                create_view_parser().parse_args(["source", "--out-dir", "out",
                                                 "--smaps-filter", "["])

    def test_execute_flow_main_uses_iteration_zero_without_creating_views(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with mock.patch("sys.argv", [
                    "execute_flow.py", "--flow", "flow.json", "--out-dir", str(
                        root),
            ]), mock.patch("execute_flow.load_flow"), mock.patch(
                    "execute_flow.load_device"), mock.patch(
                    "execute_flow.execute_iterations") as collect, mock.patch(
                    "execute_flow.lib.create_view") as create:
                self.assertEqual(execute_flow.main(), 0)

            self.assertEqual(collect.call_args.args[3], 1)
            self.assertEqual(collect.call_args.args[2], root)
            create.assert_not_called()

    def test_create_view_main_preserves_sources_and_accepts_existing_parent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with mock.patch("sys.argv", [
                    "create_view.py", "first", "second", "--out-dir", str(
                        root),
            ]), mock.patch("create_view.datetime") as now, mock.patch(
                    "create_view.lib.create_view") as create:
                now.now.return_value.strftime.return_value = "20260101_010203_000004"
                self.assertEqual(create_view.main(), 0)

            self.assertEqual(create.call_args.args[0], [
                pathlib.Path.cwd() / "first", pathlib.Path.cwd() / "second",
            ])
            self.assertEqual(create.call_args.args[1], root / "view" /
                             "view-20260101_010203_000004")

    def test_entrypoints_reset_logger_after_operation_error(self) -> None:
        with mock.patch("sys.argv", [
                "create_view.py", "evidence", "--out-dir", "out",
        ]), mock.patch("create_view.lib.create_view", side_effect=RuntimeError("bad")), \
                mock.patch("create_view.lib.reset_logger") as reset:
            self.assertEqual(create_view.main(), 1)

        reset.assert_called_once()


if __name__ == "__main__":
    unittest.main()
