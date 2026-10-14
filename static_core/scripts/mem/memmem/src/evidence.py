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

import dataclasses
import pathlib
import re

from src.metadata import (AppMetadataFile, AppSnapshotLabelPair,
                          ArtifactMetadataFile)
from src.store import EvidenceStore
from src.smaps import SmapsSummary, parse_smaps_text


@dataclasses.dataclass(frozen=True)
class ParsedEvidence:
    """Parsed source data with its evidence root retained for view provenance."""

    source: pathlib.Path
    apps: AppMetadataFile
    snapshots: ArtifactMetadataFile
    measurements: dict[AppSnapshotLabelPair, SmapsSummary]


def _load_apps(path: pathlib.Path) -> AppMetadataFile:
    if not path.is_file():
        raise RuntimeError(f"missing evidence metadata: {path}")
    return AppMetadataFile.model_validate_json(path.read_text(encoding="utf-8"))


def _load_artifacts(path: pathlib.Path) -> ArtifactMetadataFile:
    if not path.is_file():
        raise RuntimeError(f"missing evidence metadata: {path}")
    return ArtifactMetadataFile.model_validate_json(path.read_text(encoding="utf-8"))


def _validate_file(path: pathlib.Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"missing declared evidence file: {path}")
    try:
        with path.open("rb"):
            pass
    except OSError as error:
        raise RuntimeError(
            f"unreadable declared evidence file: {path}") from error


def _validate_snapshot(category: pathlib.Path, snapshot_label: str, file: str,
                       app_labels: set[str]) -> None:
    relative = pathlib.PurePosixPath(file)
    app_label = relative.stem
    expected = pathlib.PurePosixPath(snapshot_label, f"{app_label}.smaps")
    if relative != expected or app_label not in app_labels:
        raise RuntimeError(f"invalid artifact path: {file}")
    _validate_file(category.joinpath(*relative.parts))


def _validate_snapshots(metadata: ArtifactMetadataFile, category: pathlib.Path,
                        app_labels: set[str]) -> None:
    if not category.is_dir():
        raise RuntimeError(f"missing evidence directory: {category}")
    for artifact in metadata.artifacts:
        for file in artifact.files:
            _validate_snapshot(category, artifact.label, file, app_labels)


def _validate_screenshot(category: pathlib.Path, screenshot_label: str,
                         file: str) -> None:
    expected = pathlib.PurePosixPath(f"{screenshot_label}.png")
    if pathlib.PurePosixPath(file) != expected:
        raise RuntimeError(f"invalid artifact path: {file}")
    _validate_file(category.joinpath(*expected.parts))


def _validate_screenshots(metadata: ArtifactMetadataFile,
                          category: pathlib.Path) -> None:
    if not category.is_dir():
        raise RuntimeError(f"missing evidence directory: {category}")
    for artifact in metadata.artifacts:
        for file in artifact.files:
            _validate_screenshot(category, artifact.label, file)


def validate_evidence(store: EvidenceStore) -> tuple[AppMetadataFile, ArtifactMetadataFile]:
    """Validate layout only; callers own flow completion and content integrity."""
    apps = _load_apps(store.local_app_metadata_path())
    snapshots_path = store.local_artifact_metadata_path(
        store.local_snapshots_dir())
    screenshots_path = store.local_artifact_metadata_path(
        store.local_screenshots_dir())
    snapshots = _load_artifacts(snapshots_path)
    screenshots = _load_artifacts(screenshots_path)
    _validate_snapshots(snapshots, store.local_snapshots_dir(),
                        {app.label for app in apps.apps})
    _validate_screenshots(screenshots, store.local_screenshots_dir())
    return apps, snapshots


def _parse_snapshot(store: EvidenceStore, snapshot_label: str, file: str,
                    smaps_filter: re.Pattern[str]) -> tuple[AppSnapshotLabelPair, SmapsSummary] | None:
    app_label = pathlib.PurePosixPath(file).stem
    path = store.local_snapshots_dir().joinpath(*pathlib.PurePosixPath(file).parts)
    summary = parse_smaps_text(path.read_text(encoding="utf-8"), smaps_filter)
    if summary is None:
        return None
    return AppSnapshotLabelPair(app_label, snapshot_label), summary


def parse_evidence(store: EvidenceStore, smaps_filter: re.Pattern[str]) -> ParsedEvidence:
    apps, snapshots = validate_evidence(store)
    measurements: dict[AppSnapshotLabelPair, SmapsSummary] = {}
    for artifact in snapshots.artifacts:
        for file in artifact.files:
            parsed = _parse_snapshot(store, artifact.label, file, smaps_filter)
            if parsed is not None:
                pair, summary = parsed
                measurements[pair] = summary
    return ParsedEvidence(store.local_root(), apps, snapshots, measurements)


def _same_ordered_artifacts(left: ArtifactMetadataFile,
                            right: ArtifactMetadataFile) -> bool:
    return [(item.label, item.files) for item in left.artifacts] == [
        (item.label, item.files) for item in right.artifacts]


def is_evidence_equivalent(left: ParsedEvidence, right: ParsedEvidence) -> bool:
    left_apps = {app.label: (app.bundle, app.ability)
                 for app in left.apps.apps}
    right_apps = {app.label: (app.bundle, app.ability)
                  for app in right.apps.apps}
    return left_apps == right_apps and _same_ordered_artifacts(
        left.snapshots, right.snapshots)
