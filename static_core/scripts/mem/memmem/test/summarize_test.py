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
from unittest import mock

import summarize


class SummarizeTest(unittest.TestCase):
    def test_completed_iteration_stores_skips_iteration_without_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out_dir = pathlib.Path(directory)
            complete = out_dir.joinpath("iteration_0")
            incomplete = out_dir.joinpath("iteration_1")
            complete.mkdir()
            incomplete.mkdir()
            complete.joinpath("summary.csv").touch()

            with mock.patch("summarize.lib.get_logger") as get_logger:
                stores = summarize.completed_iteration_stores(out_dir)

            self.assertEqual([store.local_out_dir()
                             for store in stores], [complete])
            get_logger.return_value.warn.assert_called_once_with(
                f"skipping iteration without summary.csv: {incomplete}")

    def test_completed_iteration_stores_rejects_no_summaries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out_dir = pathlib.Path(directory)
            out_dir.joinpath("iteration_0").mkdir()

            with self.assertRaisesRegex(RuntimeError, "no iteration summary.csv"):
                summarize.completed_iteration_stores(out_dir)

    def test_main_passes_only_summary_stores_to_averaging(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out_dir = pathlib.Path(directory)
            complete = out_dir.joinpath("iteration_0")
            complete.mkdir()
            complete.joinpath("summary.csv").touch()
            out_dir.joinpath("iteration_1").mkdir()
            with mock.patch("sys.argv", ["summarize.py", "--out-dir", str(out_dir)]), \
                    mock.patch("summarize.average_reports") as average:
                self.assertEqual(summarize.main(), 0)

            stores = average.call_args.args[1]
            self.assertEqual([store.local_out_dir()
                             for store in stores], [complete])

    def test_parser_rejects_memmem_log_options(self) -> None:
        with self.assertRaises(SystemExit):
            summarize.build_parser().parse_args([
                "--out-dir", "out", "--memmem-log-level", "info",
            ])

    def test_summarize_preserves_prior_summary_on_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out_dir = pathlib.Path(directory)
            complete = out_dir.joinpath("iteration_0")
            complete.mkdir()
            complete.joinpath("summary.csv").touch()
            summary = out_dir.joinpath("summary")
            summary.mkdir()
            summary.joinpath("old.txt").write_text("old", encoding="utf-8")

            with mock.patch("summarize.average_reports",
                            side_effect=RuntimeError("failed")):
                with self.assertRaisesRegex(RuntimeError, "failed"):
                    summarize.summarize(out_dir)

            self.assertEqual(summary.joinpath(
                "old.txt").read_text(encoding="utf-8"), "old")
            self.assertFalse(out_dir.joinpath(".summary-backup").exists())
