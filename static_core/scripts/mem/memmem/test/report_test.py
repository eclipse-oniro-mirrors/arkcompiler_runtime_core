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
import unittest
from unittest import mock

from src.evidence import ParsedEvidence
from src.metadata import (AppMetadata, AppMetadataFile, ArtifactMetadata,
                          AppSnapshotLabelPair, ArtifactMetadataFile)
from src.report import SUMMARY_STATISTICS, write_view
from src.store import ViewStore
from src.smaps import SUMMARY_METRICS, MemProfile, SmapsSummary
from src.view import read_view


class ReportTest(unittest.TestCase):
    @staticmethod
    def _evidence(
        source: str,
        measurements: dict[AppSnapshotLabelPair, SmapsSummary],
        artifacts: list[ArtifactMetadata] | None = None,
    ) -> ParsedEvidence:
        return ParsedEvidence(
            source=pathlib.Path(source),
            apps=AppMetadataFile(apps=[AppMetadata(
                pid=1, label="App", bundle="com.example", ability="EntryAbility")]),
            snapshots=ArtifactMetadataFile(artifacts=artifacts or [ArtifactMetadata(
                label="start", timestamp="1", files=["start/App.smaps"])]),
            measurements=measurements,
        )

    def test_write_view_aggregates_direct_parsed_evidence(self) -> None:
        snapshots = ArtifactMetadataFile(artifacts=[ArtifactMetadata(
            label="start", timestamp="1", files=["start/App.smaps"])])
        apps = AppMetadataFile(apps=[AppMetadata(
            pid=1, label="App", bundle="com.example", ability="EntryAbility")])
        profile = MemProfile(1, 2, 3, 4, 5, 6, 7, 8, 9)
        evidence = [ParsedEvidence(
            source=pathlib.Path(f"/evidence-{index}"), apps=apps,
            snapshots=snapshots,
            measurements={AppSnapshotLabelPair("App", "start"):
                          SmapsSummary(profile, {"[anon]": profile})},
        ) for index in range(2)]
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))

            write_view(evidence, store, ".*", 0)

            view = read_view(store)
            self.assertEqual(view.summary_rows[0]["n_samples"], "2")
            self.assertEqual(view.breakdowns.artifacts[0].files, [
                             "start/App.csv"])
            self.assertEqual(view.breakdown_rows[(
                "App", "start")][0]["tag"], "[anon]")

    def test_write_view_discards_whole_outlier_and_uses_first_retained_timestamp(self) -> None:
        apps = AppMetadataFile(apps=[AppMetadata(
            pid=1, label="App", bundle="com.example", ability="EntryAbility")])
        evidence = []
        for index, value in enumerate([100, 10, 10, 10, 10]):
            profile = MemProfile(*([value] * 9))
            evidence.append(ParsedEvidence(
                source=pathlib.Path(f"/evidence-{index}"), apps=apps,
                snapshots=ArtifactMetadataFile(artifacts=[ArtifactMetadata(
                    label="start", timestamp=str(index + 1), files=["start/App.smaps"])]),
                measurements={AppSnapshotLabelPair("App", "start"):
                              SmapsSummary(profile, {"[anon]": profile})},
            ))
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))
            with mock.patch("src.report.generate_averaged_plots") as plots:
                write_view(evidence, store, ".*", 0)

            view = read_view(store)
            self.assertEqual(view.summary_rows[0]["n_samples"], "4")
            self.assertEqual(store.local_outliers_path().read_text(encoding="utf-8").splitlines(),
                             ["discarded", "/evidence-0"])
            self.assertEqual(view.breakdowns.artifacts[0].timestamp, "2")
            self.assertEqual(plots.call_args.args[2], {"start": 2})

    def test_write_view_preserves_statistics_schema_and_tag_populations(self) -> None:
        pair = AppSnapshotLabelPair("App", "start")
        zero = MemProfile(*([0] * len(SUMMARY_METRICS)))
        four = MemProfile(*([4] * len(SUMMARY_METRICS)))
        evidence = [
            self._evidence(
                "/evidence-0", {pair: SmapsSummary(zero, {"a": zero})}),
            self._evidence(
                "/evidence-1", {pair: SmapsSummary(four, {"a": four, "b": four})}),
        ]
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))
            write_view(evidence, store, ".*", re.UNICODE)

            summary = store.local_summary_path().read_text(encoding="utf-8").splitlines()
            header = summary[0].split(",")
            row = summary[1].split(",")
            self.assertEqual(
                header,
                ["app_label", "snapshot_label", "n_samples"] + [
                    f"{metric}_{statistic}" for metric in SUMMARY_METRICS
                    for statistic in SUMMARY_STATISTICS],
            )
            self.assertEqual(row[:3], ["App", "start", "2"])
            self.assertEqual(
                row[3:9], ["2.00", "0.00", "2.00", "2.83", "0", "4"])
            breakdown = store.local_breakdown_path("start", "App").read_text(
                encoding="utf-8").splitlines()
            self.assertEqual(breakdown[1].split(",")[:3], ["b", "1", "4.00"])
            self.assertEqual(breakdown[2].split(",")[:3], ["a", "2", "2.00"])
            self.assertFalse(store.local_root().joinpath(
                "summary_filtered.csv").exists())

    def test_write_view_uses_declaration_order_when_first_source_lacks_pair(self) -> None:
        early = AppSnapshotLabelPair("App", "early")
        late = AppSnapshotLabelPair("App", "late")
        profile = MemProfile(*([1] * len(SUMMARY_METRICS)))
        artifacts = [
            ArtifactMetadata(label="early", timestamp="1",
                             files=["early/App.smaps"]),
            ArtifactMetadata(label="late", timestamp="2",
                             files=["late/App.smaps"]),
        ]
        evidence = [
            self._evidence(
                "/evidence-0", {late: SmapsSummary(profile, {"tag": profile})}, artifacts),
            self._evidence(
                "/evidence-1", {early: SmapsSummary(profile, {"tag": profile})}, artifacts),
        ]
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))
            with mock.patch("src.report.generate_averaged_plots"):
                write_view(evidence, store, ".*", re.UNICODE)

            view = read_view(store)
            self.assertEqual(
                [(row["app_label"], row["snapshot_label"])
                 for row in view.summary_rows],
                [("App", "early"), ("App", "late")],
            )
            self.assertEqual(
                [artifact.label for artifact in view.breakdowns.artifacts], ["early", "late"])

    def test_empty_sources_are_scored_and_discarded_in_input_order(self) -> None:
        evidence = [self._evidence(
            f"/evidence-{index}", {}) for index in range(5)]
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))
            write_view(evidence, store, ".*", re.UNICODE)

            self.assertEqual(store.local_outliers_path().read_text(
                encoding="utf-8").splitlines(), ["discarded", "/evidence-0"])
            self.assertEqual(store.local_summary_path().read_text(
                encoding="utf-8").splitlines(), [
                    "app_label,snapshot_label,n_samples," + ",".join(
                        f"{metric}_{statistic}" for metric in SUMMARY_METRICS
                        for statistic in SUMMARY_STATISTICS)
            ])
            self.assertEqual(store.local_breakdown_metadata_path().read_text(
                encoding="utf-8").strip(), '{\n  "artifacts": []\n}')

    def test_zero_median_cells_have_zero_deviation_and_keep_tie_order(self) -> None:
        pair = AppSnapshotLabelPair("App", "start")
        evidence = []
        for index, value in enumerate([0, 0, 0, 0, 100]):
            profile = MemProfile(*([value] * len(SUMMARY_METRICS)))
            evidence.append(self._evidence(
                f"/evidence-{index}", {pair: SmapsSummary(profile, {"tag": profile})}))
        with tempfile.TemporaryDirectory() as directory:
            store = ViewStore(pathlib.Path(directory).joinpath("view"))
            with mock.patch("src.report.generate_averaged_plots"):
                write_view(evidence, store, ".*", re.UNICODE)

            self.assertEqual(store.local_outliers_path().read_text(
                encoding="utf-8").splitlines(), ["discarded", "/evidence-0"])


if __name__ == "__main__":
    unittest.main()
