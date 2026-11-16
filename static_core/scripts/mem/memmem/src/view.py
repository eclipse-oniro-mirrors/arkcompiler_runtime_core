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
from decimal import Decimal
import math
import pathlib

import pydantic

from src.evidence import _same_ordered_artifacts
from src.metadata import ArtifactMetadataFile, ViewMetadata
from src.path import normalize
from src.profile import FOOTPRINT_METRICS, SUMMARY_METRICS
from src.report import MEMDUMP_CATEGORIES, MEMDUMP_TOTAL, SUMMARY_STATISTICS
from src.store import ViewStore
from src.schema import Label


@dataclasses.dataclass(frozen=True)
class ParsedView:
    """Validated aggregate view data, independent of its provenance sources."""

    store: ViewStore
    metadata: ViewMetadata
    breakdowns: ArtifactMetadataFile
    summary_rows: list[dict[str, str]]
    footprint_rows: list[dict[str, str]]
    memdump_rows: dict[tuple[str, str], list[dict[str, str]]]
    breakdown_rows: dict[tuple[str, str], list[dict[str, str]]]


def _read_csv(path: pathlib.Path, header: list[str]) -> list[dict[str, str]]:
    if not path.is_file():
        raise ValueError(f"missing view file: {path}")
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != header:
            raise ValueError(f"invalid view CSV header: {path}")
        rows = list(reader)
    if any(None in row.values() or len(row) != len(header) for row in rows):
        raise ValueError(f"invalid view CSV row width: {path}")
    return rows


def _header(identity: list[str], metrics: list[str] = SUMMARY_METRICS) -> list[str]:
    return identity + [f"{metric}_{stat}" for metric in metrics
                       for stat in SUMMARY_STATISTICS]


def _validate_rows(rows: list[dict[str, str]], identities: list[str], path: pathlib.Path,
                   metrics: list[str] = SUMMARY_METRICS) -> None:
    for row in rows:
        _validate_row(row, identities, path, metrics)


def _validate_row(row: dict[str, str], identities: list[str], path: pathlib.Path,
                  metrics: list[str]) -> None:
    if (any(not row[identity] for identity in identities)
            or not row["n_samples"].isdecimal() or int(row["n_samples"]) <= 0):
        raise ValueError(f"invalid value in view CSV: {path}")
    for identity in identities:
        if identity not in {"tag", "category"}:
            pydantic.TypeAdapter(Label).validate_python(row[identity])
    _validate_metric_values(row, path, metrics)


def _validate_metric_values(row: dict[str, str], path: pathlib.Path,
                            metrics: list[str]) -> None:
    for metric in metrics:
        for statistic in SUMMARY_STATISTICS:
            _validate_metric_value(
                row[f"{metric}_{statistic}"], statistic, path)


def _validate_metric_value(value: str, statistic: str, path: pathlib.Path) -> None:
    if statistic in {"min", "max"}:
        if not value.isdecimal():
            raise ValueError(f"invalid value in view CSV: {path}")
        return
    try:
        numeric = float(value)
    except ValueError as error:
        raise ValueError(f"invalid value in view CSV: {path}") from error
    if not math.isfinite(numeric) or numeric < 0:
        raise ValueError(f"invalid value in view CSV: {path}")


def _declared_breakdown_identities(
        breakdowns: ArtifactMetadataFile) -> list[tuple[str, str]]:
    identities = []
    for artifact in breakdowns.artifacts:
        for file in artifact.files:
            path = pathlib.PurePosixPath(file)
            if len(path.parts) != 2 or path.parts[0] != artifact.label or path.suffix != ".csv":
                raise ValueError("invalid breakdown manifest path")
            identity = (path.stem, artifact.label)
            if path.name != f"{identity[0]}.csv" or identity in identities:
                raise ValueError("invalid breakdown manifest pair")
            identities.append(identity)
    return identities


def _validate_view_layout(store: ViewStore) -> None:
    if (not store.local_root().is_dir() or not store.local_plots_dir().is_dir()
            or not store.local_breakdowns_dir().is_dir()
            or not store.local_memdumps_dir().is_dir()
            or not store.local_metadata_path().is_file()
            or not store.local_breakdown_metadata_path().is_file()
            or not store.local_memdump_metadata_path().is_file()
            or not store.local_footprint_path().is_file()
            or store.local_root().joinpath("memdump.csv").exists()):
        raise ValueError("invalid view directory layout")


def _read_view_metadata(store: ViewStore) -> tuple[ViewMetadata, ArtifactMetadataFile,
                                                   ArtifactMetadataFile]:
    metadata = ViewMetadata.model_validate_json(
        store.local_metadata_path().read_text(encoding="utf-8"))
    breakdowns = ArtifactMetadataFile.model_validate_json(
        store.local_breakdown_metadata_path().read_text(encoding="utf-8"))
    memdumps = ArtifactMetadataFile.model_validate_json(
        store.local_memdump_metadata_path().read_text(encoding="utf-8"))
    return metadata, breakdowns, memdumps


def _read_summary_rows(store: ViewStore,
                       identities: list[tuple[str, str]]) -> list[dict[str, str]]:
    path = store.local_summary_path()
    rows = _read_csv(path, _header(
        ["app_label", "snapshot_label", "n_samples"]))
    actual = [(row["app_label"], row["snapshot_label"]) for row in rows]
    if actual != identities:
        raise ValueError("summary identities do not match breakdown metadata")
    if len(actual) != len(set(actual)):
        raise ValueError("duplicate summary identity")
    _validate_rows(rows, ["app_label", "snapshot_label"], path)
    return rows


def _read_footprint_rows(store: ViewStore, summary_rows: list[dict[str, str]]) -> list[dict[str, str]]:
    path = store.local_footprint_path()
    rows = _read_csv(path, _header(
        ["app_label", "snapshot_label", "n_samples"], FOOTPRINT_METRICS))
    identities = [(row["app_label"], row["snapshot_label"]) for row in rows]
    summary_identities = [(row["app_label"], row["snapshot_label"])
                          for row in summary_rows]
    if identities != summary_identities or len(identities) != len(set(identities)):
        raise ValueError("footprint identities do not match summary")
    if any(row["n_samples"] != summary_rows[index]["n_samples"]
           for index, row in enumerate(rows)):
        raise ValueError("footprint sample counts do not match summary")
    _validate_rows(rows, ["app_label", "snapshot_label"],
                   path, FOOTPRINT_METRICS)
    return rows


def _read_breakdown_rows(store: ViewStore,
                         identities: list[tuple[str, str]]) -> dict[tuple[str, str], list[dict[str, str]]]:
    rows: dict[tuple[str, str], list[dict[str, str]]] = {}
    for app, snapshot in identities:
        pydantic.TypeAdapter(Label).validate_python(app)
        path = store.local_breakdown_path(snapshot, app)
        values = _read_csv(path, _header(["tag", "n_samples"]))
        if not values or len({row["tag"] for row in values}) != len(values):
            raise ValueError(f"invalid breakdown rows: {path}")
        _validate_rows(values, ["tag"], path)
        rows[(app, snapshot)] = values
    return rows


def _read_memdump_rows(store: ViewStore, identities: list[tuple[str, str]]) -> dict[
        tuple[str, str], list[dict[str, str]]]:
    result = {}
    for app, snapshot in identities:
        path = store.local_memdump_path(snapshot, app)
        category_rows = _read_csv(path, _header(["category", "n_samples"]))
        actual = [row["category"] for row in category_rows]
        if actual != MEMDUMP_CATEGORIES + [MEMDUMP_TOTAL]:
            raise ValueError("memdump categories do not match expected order")
        _validate_rows(category_rows, ["category"], path)
        if any(row["n_samples"] != category_rows[0]["n_samples"] for row in category_rows):
            raise ValueError("memdump sample counts do not match")
        total = category_rows[-1]
        for metric in SUMMARY_METRICS:
            if Decimal(total[f"{metric}_mean"]) != sum(
                    (Decimal(row[f"{metric}_mean"]) for row in category_rows[:-1]), Decimal(0)):
                raise ValueError(
                    "memdump total mean does not match categories")
        result[(app, snapshot)] = category_rows
    return result


def _validate_outlier_audit(store: ViewStore, metadata: ViewMetadata) -> None:
    discarded = [row["discarded"] for row in _read_csv(
        store.local_outliers_path(), ["discarded"])]
    audit_paths = [pathlib.Path(value) for value in discarded]
    if (any(not value or not path.is_absolute() or path != normalize(path)
            for value, path in zip(discarded, audit_paths))
            or len(discarded) != len(set(discarded))
            or any(path not in metadata.dirs for path in audit_paths)):
        raise ValueError("invalid outliers.csv")


def read_view(store: ViewStore) -> ParsedView:
    """Read a strict standalone view without accessing provenance directories."""
    _validate_view_layout(store)
    metadata, breakdowns, memdumps = _read_view_metadata(store)
    identities = _declared_breakdown_identities(breakdowns)
    if _declared_breakdown_identities(memdumps) != identities or memdumps != breakdowns:
        raise ValueError("memdump metadata does not match breakdown metadata")
    summary_rows = _read_summary_rows(store, identities)
    footprint_rows = _read_footprint_rows(store, summary_rows)
    memdump_rows = _read_memdump_rows(store, identities)
    breakdown_rows = _read_breakdown_rows(store, identities)
    _validate_outlier_audit(store, metadata)
    return ParsedView(store, metadata, breakdowns, summary_rows, footprint_rows,
                      memdump_rows, breakdown_rows)


def is_view_equivalent(left: ParsedView, right: ParsedView) -> bool:
    """Compare view declarations; callers assert matching apps and flows."""
    return (left.metadata.smaps_filter == right.metadata.smaps_filter
            and left.metadata.smaps_filter_flags == right.metadata.smaps_filter_flags
            and _same_ordered_artifacts(left.breakdowns, right.breakdowns))
