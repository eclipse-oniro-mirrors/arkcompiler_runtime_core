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

import collections
import dataclasses
import pathlib
import matplotlib.pyplot as plt
import matplotlib
import matplotlib.ticker

from src.store import ViewStore
from src.smaps import SUMMARY_METRICS


matplotlib.use("Agg")


PLOT_EXTENSION = "svg"
SPARSE_LABEL_COUNT = 8
DENSE_LABEL_COUNT = 20
METRICS = [
    (metric[:-3], metric) for metric in SUMMARY_METRICS
]


@dataclasses.dataclass(frozen=True)
class AveragedStatistics:
    mean: float
    geomean: float
    median: float
    std: float
    min: int
    max: int


@dataclasses.dataclass(frozen=True)
class AveragedPlotRow:
    app_label: str
    snapshot_label: str
    metrics: dict[str, AveragedStatistics]


@dataclasses.dataclass(frozen=True)
class LabelLayout:
    rotation: int
    font_size: int
    width: float
    bottom: float


def generate_averaged_plots(
    rows: list[AveragedPlotRow],
    store: ViewStore,
    timestamps: dict[str, int],
) -> None:
    grouped: dict[str, list[AveragedPlotRow]] = collections.defaultdict(list)
    for row in rows:
        if row.snapshot_label not in timestamps:
            raise RuntimeError(
                f"missing timestamp for plotted snapshot: {row.snapshot_label}")
        grouped[row.app_label].append(row)

    for app_label, app_rows in grouped.items():
        sorted_rows = sorted(
            app_rows,
            key=lambda row: timestamps[row.snapshot_label],
        )
        first_timestamp = timestamps[sorted_rows[0].snapshot_label]
        x_values = [(timestamps[row.snapshot_label] - first_timestamp) /
                    1_000_000_000 for row in sorted_rows]
        for metric, attribute in METRICS:
            _write_plot(
                x_values=x_values,
                y_values=[
                    row.metrics[attribute].geomean for row in sorted_rows],
                snapshot_labels=[row.snapshot_label for row in sorted_rows],
                y_label=f"{metric} KB",
                title=f"{app_label} {metric}",
                path=store.local_plot_path(app_label, metric),
                y_err=[row.metrics[attribute].std for row in sorted_rows],
            )


def _write_plot(
    x_values: list[float],
    y_values: list[float | int],
    snapshot_labels: list[str],
    y_label: str,
    title: str,
    path: pathlib.Path,
    y_err: list[float] | None = None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    layout = _label_layout(len(snapshot_labels))
    figure, axes = plt.subplots(figsize=(layout.width, 4.8))
    if y_err is None:
        axes.plot(x_values, y_values, linestyle=":", marker="o")
    else:
        axes.errorbar(
            x_values,
            y_values,
            yerr=y_err,
            linestyle=":",
            marker="o",
            capsize=3,
        )
    axes.set_xlabel("seconds since first snapshot")
    axes.set_ylabel(y_label)
    axes.set_title(title)
    axes.set_xticks(x_values)
    axes.set_xticklabels(
        snapshot_labels,
        rotation=layout.rotation,
        ha="right",
        fontsize=layout.font_size,
    )
    axes.yaxis.set_major_locator(matplotlib.ticker.MaxNLocator(nbins=10))
    axes.grid(True, axis="both", linestyle="--", linewidth=0.5, alpha=0.5)
    axes.set_axisbelow(True)
    figure.tight_layout()
    figure.subplots_adjust(bottom=layout.bottom)
    figure.savefig(path, format=PLOT_EXTENSION)
    plt.close(figure)


def _label_layout(label_count: int) -> LabelLayout:
    if label_count <= SPARSE_LABEL_COUNT:
        return LabelLayout(rotation=75, font_size=9, width=8.0, bottom=0.36)
    if label_count <= DENSE_LABEL_COUNT:
        return LabelLayout(
            rotation=75,
            font_size=9,
            width=max(10.0, label_count * 0.7),
            bottom=0.42,
        )
    return LabelLayout(
        rotation=75,
        font_size=8,
        width=max(14.0, min(28.0, label_count * 0.8)),
        bottom=0.46,
    )
