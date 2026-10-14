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
from src.metadata import AppMetadataFile, ArtifactMetadataFile
from src.plot import generate_averaged_plots
from src.plot import AveragedPlotRow, AveragedStatistics
from src.report import write_view
from src.store import ViewStore
from src.smaps import SUMMARY_METRICS


class ViewOutputTest(unittest.TestCase):
    def test_empty_evidence_writes_empty_view_directories(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory).joinpath("view")
            evidence = ParsedEvidence(
                source=pathlib.Path(directory).joinpath("evidence"),
                apps=AppMetadataFile(apps=[]),
                snapshots=ArtifactMetadataFile(artifacts=[]),
                measurements={},
            )

            write_view([evidence], ViewStore(destination), ".*", re.UNICODE)

            self.assertTrue(destination.joinpath("plots").is_dir())
            self.assertTrue(destination.joinpath("breakdowns").is_dir())
            self.assertEqual(destination.joinpath("summary.csv").read_text(
                encoding="utf-8").splitlines()[0].split(",")[:3],
                ["app_label", "snapshot_label", "n_samples"])

    def test_aggregate_plots_sort_timestamps_and_use_first_plotted_origin(self) -> None:
        metrics = {metric: AveragedStatistics(1.0, 1.0, 1.0, 0.0, 1, 1)
                   for metric in SUMMARY_METRICS}
        rows = [AveragedPlotRow("App", "late", metrics),
                AveragedPlotRow("App", "early", metrics)]
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch("src.plot._write_plot") as write_plot:
                generate_averaged_plots(rows, ViewStore(pathlib.Path(directory)),
                                        {"late": 20, "early": 10})
            self.assertEqual(write_plot.call_args.kwargs["snapshot_labels"], [
                             "early", "late"])
            self.assertEqual(
                write_plot.call_args.kwargs["x_values"], [0.0, 1e-08])
            self.assertEqual(write_plot.call_args.kwargs["y_err"], [0.0, 0.0])

    def test_aggregate_plots_reject_missing_timestamp(self) -> None:
        metrics = {metric: AveragedStatistics(1.0, 1.0, 1.0, 0.0, 1, 1)
                   for metric in SUMMARY_METRICS}
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "missing timestamp"):
                generate_averaged_plots([AveragedPlotRow("App", "missing", metrics)],
                                        ViewStore(pathlib.Path(directory)), {})


if __name__ == "__main__":
    unittest.main()
