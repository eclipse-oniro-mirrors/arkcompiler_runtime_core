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
import csv
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock

import compare
from compare import build_parser, compare_reports
from src.path import absolutize
from src.report import SUMMARY_STATISTICS
from src.smaps import SUMMARY_METRICS


class CompareTest(unittest.TestCase):
    def test_compares_aggregate_views(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change_0, change_1 = (root.joinpath(name) for name in
                                        ("base", "change-0", "change-1"))
            out_dir = root.joinpath("out")
            self._write_view(base, {"size_kb": "100.00", "rss_kb": "0.00"},
                             {"present": {"size_kb": "100.00", "rss_kb": "0.00"},
                              "missing": {"size_kb": "10.00", "rss_kb": "5.00"}})
            self._write_view(change_0, {"size_kb": "140", "rss_kb": "40"},
                             {"present": {"size_kb": "140", "rss_kb": "40"},
                              "extra": {"size_kb": "1", "rss_kb": "1"}})
            self._write_view(change_1, {"size_kb": "80.00", "rss_kb": "20.00"},
                             {"present": {"size_kb": "80.00", "rss_kb": "20.00"},
                              "missing": {"size_kb": "20.00", "rss_kb": "10.00"}})

            compare_reports(base, [change_0, change_1], out_dir)

            summary = self._read_csv(out_dir.joinpath("summary.csv"))
            self.assertEqual(summary[1][:12], [
                "app", "snapshot", "100.00", "140", "40.00", "80.00", "-20.00",
                "0.00", "40", "", "20.00", "",
            ])
            breakdown = self._read_csv(out_dir.joinpath(
                "breakdowns", "snapshot", "app.csv"))
            self.assertEqual([row[0] for row in breakdown[1:]], [
                             "present", "missing", "extra"])
            self.assertEqual(breakdown[2][:6], [
                             "missing", "10.00", "", "", "20.00", "100.00"])
            self.assertEqual(breakdown[3][:6], ["extra", "", "1", "", "", ""])

    def test_rejects_incompatible_view_before_creating_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change, out_dir = root / "base", root / "change", root / "out"
            self._write_view(base, {}, {})
            self._write_view(change, {}, {}, smaps_filter="different")

            with self.assertRaisesRegex(ValueError, "incompatible"):
                compare_reports(base, [change], out_dir)
            self.assertFalse(out_dir.exists())

    def test_rejects_legacy_run_root_without_discovery(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change, out_dir = root / "base", root / "change", root / "out"
            self._write_view(base, {}, {})
            self._write_view(change / "summary", {}, {})

            with self.assertRaisesRegex(ValueError, "invalid view directory layout"):
                compare_reports(base, [change], out_dir)
            self.assertFalse(out_dir.exists())

    def test_invalid_median_leaves_no_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change, out_dir = root / "base", root / "change", root / "out"
            self._write_view(base, {}, {})
            self._write_view(change, {"size_kb": "NaN"}, {})

            with self.assertRaisesRegex(ValueError, "invalid value"):
                compare_reports(base, [change], out_dir)
            self.assertFalse(out_dir.exists())

    def test_rejects_existing_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change, out_dir = root / "base", root / "change", root / "out"
            self._write_view(base, {}, {})
            self._write_view(change, {}, {})
            out_dir.mkdir()

            with self.assertRaises(FileExistsError):
                compare_reports(base, [change], out_dir)

    def test_keeps_partial_output_when_writing_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            base, change, out_dir = root / "base", root / "change", root / "out"
            self._write_view(base, {}, {"tag": {}})
            self._write_view(change, {}, {"tag": {}})

            def fail_after_summary(path: pathlib.Path, rows: list[list[str]]) -> None:
                if path.name == "summary.csv":
                    self._write_csv(path, rows)
                    return
                raise OSError("write failed")

            with mock.patch("compare._write_csv", side_effect=fail_after_summary):
                with self.assertRaisesRegex(OSError, "write failed"):
                    compare_reports(base, [change], out_dir)
            self.assertTrue(out_dir.exists())
            self.assertTrue(out_dir.joinpath("summary.csv").is_file())

    def test_parser_rejects_abbreviated_arguments(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            build_parser().parse_args(
                ["--ba", "base", "--change", "change", "--out-dir", "out"])

    def test_parser_accepts_logging_options(self) -> None:
        args = build_parser().parse_args([
            "--base", "base", "--change", "change", "--out-dir", "out",
            "--memmem-log-level", "info", "--memmem-log-file", "memmem.log",
        ])

        self.assertEqual(args.memmem_log_level, "info")
        self.assertEqual(args.memmem_log_file, pathlib.Path("memmem.log"))

    def test_main_preserves_change_argument_order(self) -> None:
        with mock.patch("sys.argv", [
                "compare.py", "--base", "base", "--change", "first",
                "--change", "second", "--out-dir", "out",
        ]), mock.patch("compare.compare_reports") as cmp:
            self.assertEqual(__import__("compare").main(), 0)
        self.assertEqual([path.name for path in cmp.call_args.args[1]], [
                         "first", "second"])

    def test_main_resets_logger_after_error(self) -> None:
        with mock.patch("sys.argv", [
                "compare.py", "--base", "base", "--change", "change",
                "--out-dir", "out", "--memmem-log-level", "info",
                "--memmem-log-file", "memmem.log",
        ]), mock.patch("compare.compare_reports", side_effect=ValueError("failed")), \
                mock.patch("compare.lib.configure_logger") as configure, \
                mock.patch("compare.lib.reset_logger") as reset, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(compare.main(), 1)

        configure.assert_called_once_with(
            "info", absolutize(pathlib.Path("memmem.log")))
        reset.assert_called_once_with()

    def _write_view(self, root: pathlib.Path, metrics: dict[str, str],
                    tags: dict[str, dict[str, str]], *, smaps_filter: str = ".*") -> None:
        if not tags:
            tags = {"tag": {}}
        root.mkdir(parents=True)
        root.joinpath("plots").mkdir()
        root.joinpath("breakdowns", "snapshot").mkdir(parents=True)
        root.joinpath("metadata.json").write_text(json.dumps({
            "dirs": ["/unavailable/evidence"], "smaps_filter": smaps_filter,
            "smaps_filter_flags": 32,
        }), encoding="utf-8")
        root.joinpath("outliers.csv").write_text(
            "discarded\n", encoding="utf-8")
        self._write_csv(root / "summary.csv", [
            self._header(["app_label", "snapshot_label", "n_samples"]),
            ["app", "snapshot", "1", *self._statistics(metrics)],
        ])
        root.joinpath("breakdowns", "metadata.json").write_text(json.dumps({
            "artifacts": [{"label": "snapshot", "timestamp": "0",
                           "files": ["snapshot/app.csv"]}],
        }), encoding="utf-8")
        self._write_csv(root / "breakdowns" / "snapshot" / "app.csv", [
            self._header(["tag", "n_samples"]),
            *[[tag, "1", *self._statistics(values)]
              for tag, values in tags.items()],
        ])

    def _header(self, identity: list[str]) -> list[str]:
        return identity + [f"{metric}_{stat}" for metric in SUMMARY_METRICS
                           for stat in SUMMARY_STATISTICS]

    def _statistics(self, metrics: dict[str, str]) -> list[str]:
        return [metrics.get(metric, "1") if statistic == "median" else "1"
                for metric in SUMMARY_METRICS for statistic in SUMMARY_STATISTICS]

    def _write_csv(self, path: pathlib.Path, rows: list[list[str]]) -> None:
        with path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def _read_csv(self, path: pathlib.Path) -> list[list[str]]:
        with path.open(encoding="utf-8", newline="") as stream:
            return list(csv.reader(stream))


if __name__ == "__main__":
    unittest.main()
