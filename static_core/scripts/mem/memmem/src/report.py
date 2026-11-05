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
import math
import pathlib
import statistics
from collections import defaultdict

from src.evidence import ParsedEvidence
from src.metadata import AppSnapshotLabelPair, ArtifactMetadata, ArtifactMetadataFile, ViewMetadata
from src.plot import AveragedPlotRow, AveragedStatistics, generate_averaged_plots
from src.store import ViewStore
from src.smaps import SUMMARY_METRICS, SmapsSummary


SUMMARY_STATISTICS = [
    field.name for field in dataclasses.fields(AveragedStatistics)]


def _statistics(values: list[int]) -> AveragedStatistics:
    return AveragedStatistics(
        mean=statistics.mean(values),
        geomean=0.0 if 0 in values else statistics.geometric_mean(values),
        median=statistics.median(values),
        std=statistics.stdev(values) if len(values) > 1 else 0.0,
        min=min(values), max=max(values),
    )


def _header(identity: list[str]) -> list[str]:
    return identity + [f"{metric}_{stat}" for metric in SUMMARY_METRICS
                       for stat in SUMMARY_STATISTICS]


def _summarize_metrics(evidence: ParsedEvidence) -> dict[tuple[AppSnapshotLabelPair, str, str], int]:
    cells: dict[tuple[AppSnapshotLabelPair, str, str], int] = {}
    for pair, summary in evidence.measurements.items():
        for metric in SUMMARY_METRICS:
            cells[(pair, "", metric)] = getattr(summary.total, metric)
        for tag, profile in summary.breakdown.items():
            for metric in SUMMARY_METRICS:
                cells[(pair, tag, metric)] = getattr(profile, metric)
    return cells


def _filter_outliers(evidence: list[ParsedEvidence]) -> list[ParsedEvidence]:
    cells = [_summarize_metrics(item) for item in evidence]
    medians = {
        cell: statistics.median(values)
        for cell in set().union(*cells)
        if (values := [source[cell] for source in cells if cell in source])
    }
    scores = []
    for source in cells:
        deviations = [0.0 if medians[cell] == 0 else abs(value - medians[cell]) / abs(medians[cell])
                      for cell, value in source.items()]
        scores.append(statistics.median(deviations) if deviations else 0.0)
    count = len(evidence)
    discarded_count = max(0, min(math.floor(0.20 * count), count - 2,
                                 count - math.ceil(0.60 * count)))
    discarded = {index for index, _ in sorted(
        enumerate(scores), key=lambda item: (-item[1], item[0]))[:discarded_count]}
    return [item for index, item in enumerate(evidence)
            if index not in discarded]


def write_view(evidence: list[ParsedEvidence], store: ViewStore,
               pattern: str, flags: int) -> None:
    _initialize_view(store, evidence, pattern, flags)
    retained = _filter_outliers(evidence)
    reference = retained[0]
    rows, plot_rows, artifacts = _aggregate_rows(reference, retained, store)
    _write_summary(store, rows)
    _write_outliers(store, evidence, retained)
    _write_breakdown_metadata(store, artifacts)
    generate_averaged_plots(plot_rows, store,
                            {item.label: int(item.timestamp)
                             for item in reference.snapshots.artifacts})


def _initialize_view(store: ViewStore, evidence: list[ParsedEvidence],
                     pattern: str, flags: int) -> None:
    store.local_root().mkdir(parents=True, exist_ok=False)
    store.local_breakdowns_dir().mkdir()
    store.local_plots_dir().mkdir()
    store.local_metadata_path().write_text(
        ViewMetadata(dirs=[item.source for item in evidence], smaps_filter=pattern,
                     smaps_filter_flags=flags).model_dump_json(indent=2) + "\n",
        encoding="utf-8")


def _aggregate_rows(reference: ParsedEvidence, retained: list[ParsedEvidence],
                    store: ViewStore) -> tuple[list[list[str]], list[AveragedPlotRow], list[ArtifactMetadata]]:
    rows: list[list[str]] = []
    plot_rows: list[AveragedPlotRow] = []
    artifacts: list[ArtifactMetadata] = []
    for event in reference.snapshots.artifacts:
        event_rows, event_plots, artifact = _aggregate_snapshot(
            event, retained, store)
        rows.extend(event_rows)
        plot_rows.extend(event_plots)
        if artifact is not None:
            artifacts.append(artifact)
    return rows, plot_rows, artifacts


def _aggregate_snapshot(event: ArtifactMetadata, retained: list[ParsedEvidence],
                        store: ViewStore) -> tuple[list[list[str]], list[AveragedPlotRow], ArtifactMetadata | None]:
    rows: list[list[str]] = []
    plot_rows: list[AveragedPlotRow] = []
    files: list[str] = []
    for file in event.files:
        app = pathlib.PurePosixPath(file).stem
        pair = AppSnapshotLabelPair(app, event.label)
        summaries = [item.measurements[pair] for item in retained
                     if pair in item.measurements]
        if not summaries:
            continue
        values = {metric: [getattr(summary.total, metric) for summary in summaries]
                  for metric in SUMMARY_METRICS}
        stats = {metric: _statistics(metric_values)
                 for metric, metric_values in values.items()}
        rows.append(_summary_row(app, event.label, summaries, stats))
        plot_rows.append(AveragedPlotRow(app, event.label, stats))
        files.append(f"{event.label}/{app}.csv")
        _write_breakdown(store.local_breakdown_path(
            event.label, app), summaries)
    artifact = ArtifactMetadata(label=event.label, timestamp=event.timestamp,
                                files=files) if files else None
    return rows, plot_rows, artifact


def _summary_row(app: str, snapshot: str, summaries: list[SmapsSummary],
                 statistics_by_metric: dict[str, AveragedStatistics]) -> list[str]:
    row = [app, snapshot, str(len(summaries))]
    for metric in SUMMARY_METRICS:
        item = statistics_by_metric[metric]
        row.extend([f"{item.mean:.2f}", f"{item.geomean:.2f}",
                    f"{item.median:.2f}", f"{item.std:.2f}",
                    str(item.min), str(item.max)])
    return row


def _write_summary(store: ViewStore, rows: list[list[str]]) -> None:
    with store.local_summary_path().open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(_header(["app_label", "snapshot_label", "n_samples"]))
        writer.writerows(rows)


def _write_outliers(store: ViewStore, evidence: list[ParsedEvidence],
                    retained_evidence: list[ParsedEvidence]) -> None:
    retained_sources = {item.source for item in retained_evidence}
    with store.local_outliers_path().open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["discarded"])
        writer.writerows([[str(item.source)] for item in evidence
                          if item.source not in retained_sources])


def _write_breakdown_metadata(store: ViewStore,
                              artifacts: list[ArtifactMetadata]) -> None:
    store.local_breakdown_metadata_path().write_text(
        ArtifactMetadataFile(
            artifacts=artifacts).model_dump_json(indent=2) + "\n",
        encoding="utf-8")


def _write_breakdown(path: pathlib.Path, summaries: list[SmapsSummary]) -> None:
    values: dict[str, dict[str, list[int]]] = defaultdict(
        lambda: {metric: [] for metric in SUMMARY_METRICS})
    for summary in summaries:
        for tag, profile in summary.breakdown.items():
            for metric in SUMMARY_METRICS:
                values[tag][metric].append(getattr(profile, metric))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(_header(["tag", "n_samples"]))
        for tag, metric_values in sorted(values.items(),
                                         key=lambda item: (-statistics.mean(item[1]["size_kb"]), item[0])):
            row = [tag, str(len(metric_values["size_kb"]))]
            for metric in SUMMARY_METRICS:
                item = _statistics(metric_values[metric])
                row.extend([f"{item.mean:.2f}", f"{item.geomean:.2f}",
                            f"{item.median:.2f}", f"{item.std:.2f}",
                            str(item.min), str(item.max)])
            writer.writerow(row)
