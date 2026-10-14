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

import pydantic

from src.evidence import _same_ordered_artifacts
from src.metadata import ArtifactMetadataFile, ViewMetadata
from src.path import normalize
from src.report import SUMMARY_STATISTICS
from src.store import ViewStore
from src.schema import Label
from src.smaps import SUMMARY_METRICS


@dataclasses.dataclass(frozen=True)
class ParsedView:
    """Validated aggregate view data, independent of its provenance sources."""

    store: ViewStore
    metadata: ViewMetadata
    breakdowns: ArtifactMetadataFile
    summary_rows: list[dict[str, str]]
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


def _header(identity: list[str]) -> list[str]:
    return identity + [f"{metric}_{stat}" for metric in SUMMARY_METRICS
                       for stat in SUMMARY_STATISTICS]


def _validate_rows(rows: list[dict[str, str]], identities: list[str], path: pathlib.Path) -> None:
    for row in rows:
        _validate_row(row, identities, path)


def _validate_row(row: dict[str, str], identities: list[str], path: pathlib.Path) -> None:
    if (any(not row[identity] for identity in identities)
            or not row["n_samples"].isdecimal() or int(row["n_samples"]) <= 0):
        raise ValueError(f"invalid value in view CSV: {path}")
    for identity in identities:
        if identity != "tag":
            pydantic.TypeAdapter(Label).validate_python(row[identity])
    _validate_metric_values(row, path)


def _validate_metric_values(row: dict[str, str], path: pathlib.Path) -> None:
    for metric in SUMMARY_METRICS:
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
            or not store.local_metadata_path().is_file()
            or not store.local_breakdown_metadata_path().is_file()):
        raise ValueError("invalid view directory layout")


def _read_view_metadata(store: ViewStore) -> tuple[ViewMetadata, ArtifactMetadataFile]:
    metadata = ViewMetadata.model_validate_json(
        store.local_metadata_path().read_text(encoding="utf-8"))
    breakdowns = ArtifactMetadataFile.model_validate_json(
        store.local_breakdown_metadata_path().read_text(encoding="utf-8"))
    return metadata, breakdowns


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
    metadata, breakdowns = _read_view_metadata(store)
    identities = _declared_breakdown_identities(breakdowns)
    summary_rows = _read_summary_rows(store, identities)
    breakdown_rows = _read_breakdown_rows(store, identities)
    _validate_outlier_audit(store, metadata)
    return ParsedView(store, metadata, breakdowns, summary_rows, breakdown_rows)


def is_view_equivalent(left: ParsedView, right: ParsedView) -> bool:
    """Compare view declarations; callers assert matching apps and flows."""
    return (left.metadata.smaps_filter == right.metadata.smaps_filter
            and left.metadata.smaps_filter_flags == right.metadata.smaps_filter_flags
            and _same_ordered_artifacts(left.breakdowns, right.breakdowns))
