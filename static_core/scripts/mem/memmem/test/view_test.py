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
import dataclasses
import json
import pathlib
import re
import tempfile
import unittest
from collections.abc import Callable

from compare import compare_reports
from src.metadata import AppMetadata, AppMetadataFile, ArtifactMetadata, ArtifactMetadataFile
from src.report import SUMMARY_STATISTICS
from src.store import EvidenceStore, ViewStore
from src.smaps import SUMMARY_METRICS
from src.view import is_view_equivalent, read_view

import lib


class ViewTest(unittest.TestCase):
    def test_reader_requires_eager_directories_even_for_empty_views(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self._write_view(root.joinpath("view"))
            for name in ("plots", "breakdowns"):
                view = root.joinpath("view")
                target = view.joinpath(name)
                if target.is_dir():
                    target.rmdir() if name == "plots" else None
                if name == "breakdowns":
                    target.joinpath("start", "App.csv").unlink()
                    target.joinpath("start").rmdir()
                    target.joinpath("metadata.json").unlink()
                    target.rmdir()
                target.write_text("not a directory", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "directory layout"):
                    read_view(ViewStore(view))
                target.unlink()
                if name == "plots":
                    target.mkdir()
                else:
                    target.mkdir()
                    target.joinpath("start").mkdir()
                    self._write_csv(target.joinpath("start", "App.csv"), [
                        self._header(["tag", "n_samples"]),
                        ["tag", "1", *self._statistics()],
                    ])
                    target.joinpath("metadata.json").write_text(json.dumps({
                        "artifacts": [{"label": "start", "timestamp": "0",
                                       "files": ["start/App.csv"]}],
                    }), encoding="utf-8")

    def test_reader_rejects_every_invalid_numeric_column(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for column in range(len(self._statistics())):
                view = root.joinpath(str(column))
                self._write_view(view)
                path = view.joinpath("summary.csv")
                with path.open(encoding="utf-8", newline="") as stream:
                    rows = list(csv.reader(stream))
                rows[1][3 + column] = "NaN" if column % 6 != 4 else "-1"
                self._write_csv(path, rows)
                with self.assertRaisesRegex(ValueError, "invalid value"):
                    read_view(ViewStore(view))

    def test_reader_rejects_bad_headers_widths_duplicates_and_audit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            cases: dict[str, Callable[[pathlib.Path], object]] = {
                "header": lambda view: view.joinpath("summary.csv").write_text(
                    "legacy\n", encoding="utf-8"),
                "width": lambda view: self._write_csv(view.joinpath("summary.csv"), [
                    self._header(["app_label", "snapshot_label", "n_samples"]), ["App"]]),
                "tags": lambda view: self._write_csv(view.joinpath(
                    "breakdowns", "start", "App.csv"), [
                    self._header(["tag", "n_samples"]),
                    ["tag", "1", *self._statistics()], ["tag", "1", *self._statistics()]]),
                "audit": lambda view: view.joinpath("outliers.csv").write_text(
                    "discarded\n/unrelated\n", encoding="utf-8"),
            }
            for name, mutate in cases.items():
                view = root.joinpath(name)
                self._write_view(view)
                mutate(view)
                with self.assertRaises(ValueError):
                    read_view(ViewStore(view))

    def test_reader_rejects_legacy_and_invalid_breakdown_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, metadata in {
                "legacy": {"files": []},
                "timestamp": {"artifacts": [{"label": "start", "timestamp": "-1",
                                             "files": ["start/App.csv"]}]},
                "path": {"artifacts": [{"label": "start", "timestamp": "0",
                                        "files": ["other/App.csv"]}]},
            }.items():
                view = root.joinpath(name)
                self._write_view(view)
                view.joinpath("breakdowns", "metadata.json").write_text(
                    json.dumps(metadata), encoding="utf-8")
                with self.assertRaises(Exception):
                    read_view(ViewStore(view))

    def test_reader_ignores_undeclared_breakdowns_and_plot_contents(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            view = pathlib.Path(directory).joinpath("view")
            self._write_view(view)
            view.joinpath("plots", "ignored.svg").write_text(
                "not SVG", encoding="utf-8")
            view.joinpath("breakdowns", "extra.csv").write_text(
                "legacy", encoding="utf-8")
            self.assertEqual(len(read_view(ViewStore(view)).summary_rows), 1)

    def test_equivalence_honors_order_and_flags_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self._write_view(root.joinpath("left"))
            self._write_view(root.joinpath("right"))
            left = read_view(ViewStore(root.joinpath("left")))
            right = read_view(ViewStore(root.joinpath("right")))
            self.assertTrue(is_view_equivalent(left, right))
            data = json.loads(root.joinpath(
                "right", "metadata.json").read_text())
            data["smaps_filter_flags"] = re.IGNORECASE
            root.joinpath("right", "metadata.json").write_text(
                json.dumps(data), encoding="utf-8")
            self.assertFalse(is_view_equivalent(
                left, read_view(ViewStore(root / "right"))))
            reordered = ArtifactMetadataFile(artifacts=[
                ArtifactMetadata(label="later", timestamp="1",
                                 files=["later/App.csv"]),
                ArtifactMetadata(label="earlier", timestamp="0",
                                 files=["earlier/App.csv"]),
            ])
            reordered_files = ArtifactMetadataFile(artifacts=[ArtifactMetadata(
                label="later", timestamp="1", files=["later/Other.csv", "later/App.csv"]),
                ArtifactMetadata(label="earlier", timestamp="0",
                                 files=["earlier/App.csv"]),
            ])
            self.assertFalse(is_view_equivalent(dataclasses.replace(left, breakdowns=reordered),
                                                dataclasses.replace(left, breakdowns=ArtifactMetadataFile(
                                                    artifacts=list(reversed(reordered.artifacts))))))
            self.assertFalse(is_view_equivalent(dataclasses.replace(left, breakdowns=reordered),
                                                dataclasses.replace(left, breakdowns=reordered_files)))

    def test_generated_views_are_source_free_round_trips_and_comparisons(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self._write_evidence(root.joinpath("base-evidence"), "[base]")
            self._write_evidence(root.joinpath("change-evidence"), "[change]")
            base = lib.create_view(
                [root / "base-evidence"], root / "base-view")
            change = lib.create_view(
                [root / "change-evidence"], root / "change-view")
            for evidence in (root / "base-evidence", root / "change-evidence"):
                for path in sorted(evidence.rglob("*"), reverse=True):
                    path.unlink() if path.is_file() else path.rmdir()
                evidence.rmdir()
            self.assertTrue(is_view_equivalent(read_view(ViewStore(base)),
                                               read_view(ViewStore(change))))
            compare_reports(base, [change], root / "comparison")
            self.assertTrue((root / "comparison" / "summary.csv").is_file())

    def test_equivalent_evidence_can_create_incompatible_views(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self._write_evidence(root.joinpath("left-evidence"), "[match]")
            self._write_evidence(root.joinpath("right-evidence"), "[other]")
            pattern = re.compile(r"^\[match\]$")
            left = lib.create_view(
                [root / "left-evidence"], root / "left-view", pattern)
            right = lib.create_view(
                [root / "right-evidence"], root / "right-view", pattern)
            self.assertFalse(is_view_equivalent(read_view(ViewStore(left)),
                                                read_view(ViewStore(right))))
            with self.assertRaisesRegex(ValueError, "incompatible"):
                compare_reports(left, [right], root / "comparison")

    def _header(self, identity: list[str]) -> list[str]:
        return identity + [f"{metric}_{statistic}" for metric in SUMMARY_METRICS
                           for statistic in SUMMARY_STATISTICS]

    def _statistics(self) -> list[str]:
        return ["1" for _ in SUMMARY_METRICS for _ in SUMMARY_STATISTICS]

    def _write_csv(self, path: pathlib.Path, rows: list[list[str]]) -> None:
        with path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def _write_view(self, root: pathlib.Path) -> None:
        root.mkdir()
        root.joinpath("plots").mkdir()
        root.joinpath("breakdowns", "start").mkdir(parents=True)
        root.joinpath("metadata.json").write_text(json.dumps({
            "dirs": ["/unavailable/evidence"], "smaps_filter": ".*",
            "smaps_filter_flags": re.UNICODE,
        }), encoding="utf-8")
        root.joinpath("outliers.csv").write_text(
            "discarded\n", encoding="utf-8")
        root.joinpath("breakdowns", "metadata.json").write_text(json.dumps({
            "artifacts": [{"label": "start", "timestamp": "0",
                           "files": ["start/App.csv"]}],
        }), encoding="utf-8")
        self._write_csv(root.joinpath("summary.csv"), [
            self._header(["app_label", "snapshot_label", "n_samples"]),
            ["App", "start", "1", *self._statistics()],
        ])
        self._write_csv(root.joinpath("breakdowns", "start", "App.csv"), [
            self._header(["tag", "n_samples"]),
            ["tag", "1", *self._statistics()],
        ])

    def _write_evidence(self, root: pathlib.Path, tag: str) -> None:
        store = EvidenceStore(root)
        root.mkdir()
        store.local_snapshots_dir().mkdir()
        store.local_screenshots_dir().mkdir()
        store.local_app_metadata_path().write_text(AppMetadataFile(apps=[AppMetadata(
            pid=1, label="App", bundle="bundle", ability="ability")]).model_dump_json(),
            encoding="utf-8")
        snapshots = ArtifactMetadataFile(artifacts=[ArtifactMetadata(
            label="start", timestamp="1", files=["start/App.smaps"])])
        store.local_artifact_metadata_path(store.local_snapshots_dir()).write_text(
            snapshots.model_dump_json(), encoding="utf-8")
        store.local_artifact_metadata_path(store.local_screenshots_dir()).write_text(
            ArtifactMetadataFile(artifacts=[]).model_dump_json(), encoding="utf-8")
        snapshot = store.local_snapshot_path("start", "App")
        snapshot.parent.mkdir()
        snapshot.write_text(
            f"1-2 rw-p 0 00:00 0 {tag}\nSize: 1 kB\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
