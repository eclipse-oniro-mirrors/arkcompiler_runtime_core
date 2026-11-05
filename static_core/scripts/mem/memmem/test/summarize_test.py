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
import unittest

from create_view import build_parser as create_view_parser
from execute_flow import build_parser as execute_flow_parser


class CliAcceptanceTest(unittest.TestCase):
    def test_execute_flow_accepts_execution_options(self) -> None:
        args = execute_flow_parser().parse_args([
            "--flow", "flow.json", "--out-dir", "evidence", "--repeats", "2",
            "--no-device-log",
        ])

        self.assertEqual(args.out_dir.name, "evidence")
        self.assertEqual(args.repeats, 2)
        self.assertFalse(args.device_log)

    def test_create_view_requires_sources_and_destination(self) -> None:
        parser = create_view_parser()
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                parser.parse_args([])
            with self.assertRaises(SystemExit):
                parser.parse_args(["evidence"])


if __name__ == "__main__":
    unittest.main()
