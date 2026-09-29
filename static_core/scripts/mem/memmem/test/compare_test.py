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

import csv
import pathlib
import tempfile
import unittest
from unittest import mock

from compare import compare_reports
from src.smaps import SUMMARY_METRICS


class CompareTest(unittest.TestCase):
    def test_compares_filtered_and_unfiltered_reports(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change_0 = root.joinpath("change-0")
            change_1 = root.joinpath("change-1")
            out_dir = root.joinpath("out")
            self._write_report(
                base,
                {"size_kb": "100.00", "rss_kb": "0.00"},
                {"present": {"size_kb": "100.00", "rss_kb": "0.00"},
                 "missing": {"size_kb": "10.00", "rss_kb": "5.00"}},
                filtered=True,
            )
            self._write_report(
                change_0,
                {"size_kb": "140", "rss_kb": "40"},
                {"present": {"size_kb": "140", "rss_kb": "40"},
                 "extra": {"size_kb": "1", "rss_kb": "1"}},
            )
            self._write_report(
                change_1,
                {"size_kb": "80.00", "rss_kb": "20.00"},
                {"present": {"size_kb": "80.00", "rss_kb": "20.00"},
                 "missing": {"size_kb": "20.00", "rss_kb": "10.00"}},
                filtered=True,
            )

            compare_reports(base, [change_0, change_1], out_dir)

            summary = self._read_csv(out_dir.joinpath("summary.csv"))
            self.assertEqual(summary[0][:8], [
                "app_label", "snapshot_label", "size_kb_base",
                "size_kb_change_0", "size_kb_change_0_pct",
                "size_kb_change_1", "size_kb_change_1_pct", "rss_kb_base",
            ])
            self.assertEqual(summary[1][:7], [
                "app", "snapshot", "100.00", "140", "40.00", "80.00", "-20.00",
            ])
            self.assertEqual(summary[1][7:12], ["0.00", "40", "", "20.00", ""])

            breakdown = self._read_csv(
                out_dir.joinpath("breakdowns", "snapshot", "app-snapshot.csv"))
            self.assertEqual(breakdown[1][:6], [
                "present", "100.00", "140", "40.00", "80.00", "-20.00",
            ])
            self.assertEqual(breakdown[2][:6], [
                "missing", "10.00", "", "", "20.00", "100.00",
            ])
            self.assertEqual(breakdown[3][:6], [
                "extra", "", "1", "", "", "",
            ])
            self.assertEqual(len(breakdown), 4)

    def test_rejects_mismatched_summary_labels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            self._write_report(base, {}, {})
            self._write_report(change, {}, {}, app_label="other")

            with self.assertRaisesRegex(ValueError, "app_label"):
                compare_reports(base, [change], root.joinpath("out"))

    def test_rejects_mismatched_breakdown_layout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            self._write_report(base, {}, {})
            self._write_report(change, {}, {}, breakdown_label="other")

            with self.assertRaisesRegex(ValueError, "breakdowns layout"):
                compare_reports(base, [change], root.joinpath("out"))

    def test_invalid_input_leaves_no_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            out_dir = root.joinpath("out")
            self._write_report(base, {}, {})
            self._write_report(change, {"size_kb": "NaN"}, {})

            with self.assertRaisesRegex(ValueError, "invalid value"):
                compare_reports(base, [change], out_dir)

            self.assertFalse(out_dir.exists())

    def test_missing_report_leaves_no_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            out_dir = root.joinpath("out")
            self._write_report(base, {}, {})
            change.mkdir()

            with self.assertRaisesRegex(ValueError, "missing summary.csv"):
                compare_reports(base, [change], out_dir)

            self.assertFalse(out_dir.exists())

    def test_malformed_report_leaves_no_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            out_dir = root.joinpath("out")
            self._write_report(base, {}, {})
            change.mkdir()
            change.joinpath("summary.csv").write_text(
                "app_label,app_label\napp,app\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "duplicate header"):
                compare_reports(base, [change], out_dir)

            self.assertFalse(out_dir.exists())

    def test_rejects_existing_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base = root.joinpath("base")
            change = root.joinpath("change")
            out_dir = root.joinpath("out")
            self._write_report(base, {}, {})
            self._write_report(change, {}, {})
            out_dir.mkdir()

            with self.assertRaisesRegex(FileExistsError, "exists"):
                compare_reports(base, [change], out_dir)

    def test_main_preserves_change_argument_order(self) -> None:
        with mock.patch("sys.argv", [
                "compare.py", "--base", "base", "--change", "first",
                "--change", "second", "--out-dir", "out",
        ]), mock.patch("compare.compare_reports") as compare:
            self.assertEqual(__import__("compare").main(), 0)

        self.assertEqual([path.name for path in compare.call_args.args[1]],
                         ["first", "second"])

    def _write_report(
        self,
        out_dir: pathlib.Path,
        metrics: dict[str, str],
        tags: dict[str, dict[str, str]],
        *,
        filtered: bool = False,
        app_label: str = "app",
        breakdown_label: str = "snapshot",
    ) -> None:
        report_dir = out_dir.joinpath("summary") if filtered else out_dir
        report_dir.mkdir(parents=True)
        standard_header = ["app_label", "snapshot_label", *SUMMARY_METRICS]
        standard_row = [app_label, "snapshot"] + [metrics.get(metric, "1")
                                                  for metric in SUMMARY_METRICS]
        self._write_csv(report_dir.joinpath("summary.csv"), [
            standard_header,
            standard_row,
        ])
        if filtered:
            filtered_header = [
                "app_label", "snapshot_label", "n_samples",
                *[f"{metric}_median" for metric in SUMMARY_METRICS],
            ]
            filtered_row = [app_label, "snapshot", "2"] + [
                metrics.get(metric, "1.00") for metric in SUMMARY_METRICS]
            self._write_csv(report_dir.joinpath("summary_filtered.csv"), [
                filtered_header,
                filtered_row,
            ])
        breakdown_header = (
            ["tag", "n_samples", *
                [f"{metric}_median" for metric in SUMMARY_METRICS]]
            if filtered else ["tag", *SUMMARY_METRICS]
        )
        breakdown_rows = [breakdown_header]
        for tag, tag_metrics in tags.items():
            breakdown_rows.append(
                [tag, "2"] + [tag_metrics.get(metric, "1.00")
                              for metric in SUMMARY_METRICS]
                if filtered else [tag] + [tag_metrics.get(metric, "1")
                                          for metric in SUMMARY_METRICS])
        self._write_csv(report_dir.joinpath(
            "breakdowns", breakdown_label, f"{app_label}-{breakdown_label}.csv"),
            breakdown_rows)

    def _write_csv(self, path: pathlib.Path, rows: list[list[str]]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def _read_csv(self, path: pathlib.Path) -> list[list[str]]:
        with path.open(encoding="utf-8", newline="") as stream:
            return list(csv.reader(stream))


if __name__ == "__main__":
    unittest.main()
