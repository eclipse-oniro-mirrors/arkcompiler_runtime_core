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
import unittest

import pydantic

from src.metadata import (
    AppMetadata,
    AppMetadataFile,
    ArtifactMetadata,
    ArtifactMetadataFile,
    ViewMetadata,
)


class MetadataTest(unittest.TestCase):
    def test_metadata_models_serialize(self) -> None:
        app = AppMetadata(
            pid=123,
            label="App",
            bundle="com.example",
            ability="EntryAbility",
        )
        app_metadata = AppMetadataFile(apps=[app])
        artifact_metadata = ArtifactMetadataFile(
            artifacts=[
                ArtifactMetadata(
                    label="after_start",
                    timestamp="1700000000000000000",
                    files=["after_start/App.smaps"],
                )
            ]
        )

        self.assertEqual(AppMetadataFile.model_validate_json(
            app_metadata.model_dump_json()), app_metadata)
        self.assertEqual(
            ArtifactMetadataFile.model_validate_json(
                artifact_metadata.model_dump_json()),
            artifact_metadata,
        )

    def test_empty_artifact_collection_is_valid(self) -> None:
        self.assertEqual(ArtifactMetadataFile(artifacts=[]).artifacts, [])
        self.assertEqual(AppMetadataFile(apps=[]).apps, [])

    def test_artifact_files_are_required_and_nonempty(self) -> None:
        for artifact in (
            {"label": "snapshot", "timestamp": "1"},
            {"label": "snapshot", "timestamp": "1", "files": []},
        ):
            with self.assertRaises(pydantic.ValidationError):
                ArtifactMetadata.model_validate(artifact)

    def test_legacy_metadata_fields_are_rejected(self) -> None:
        legacy_metadata: tuple[dict[str, object], ...] = (
            {"artifacts": [{"label": "snapshot", "timestamp": "1"}]},
            {"events": []},
            {"artifacts": [], "run": "legacy"},
        )
        for metadata in legacy_metadata:
            with self.assertRaises(pydantic.ValidationError):
                ArtifactMetadataFile.model_validate(metadata)

    def test_artifact_metadata_rejects_duplicate_identities(self) -> None:
        artifact = {"label": "snapshot", "timestamp": "1",
                    "files": ["snapshot/App.smaps"]}
        with self.assertRaises(pydantic.ValidationError):
            ArtifactMetadataFile.model_validate(
                {"artifacts": [artifact, artifact]})
        with self.assertRaises(pydantic.ValidationError):
            AppMetadataFile.model_validate({"apps": [
                {"pid": 1, "label": "App", "bundle": "bundle", "ability": "ability"},
                {"pid": 2, "label": "App", "bundle": "bundle", "ability": "ability"},
            ]})

    def test_artifact_metadata_rejects_nonportable_paths(self) -> None:
        for path in ("../App.smaps", "/tmp/App.smaps", "snapshot\\App.smaps"):
            with self.assertRaises(pydantic.ValidationError):
                ArtifactMetadata(label="snapshot", timestamp="1", files=[path])

    def test_view_metadata_normalizes_and_rejects_duplicates(self) -> None:
        pattern = re.compile("app", re.IGNORECASE)
        metadata = ViewMetadata(
            dirs=[pathlib.Path("/tmp/a/../evidence")],
            smaps_filter=pattern.pattern,
            smaps_filter_flags=re.IGNORECASE,
        )
        self.assertEqual(metadata.dirs, [pathlib.Path("/tmp/evidence")])
        self.assertEqual(metadata.smaps_filter_flags, pattern.flags)
        self.assertEqual(
            ViewMetadata.model_validate_json(metadata.model_dump_json()), metadata)
        with self.assertRaises(pydantic.ValidationError):
            ViewMetadata(
                dirs=[pathlib.Path("/tmp/evidence"),
                      pathlib.Path("/tmp/a/../evidence")],
                smaps_filter=".*",
                smaps_filter_flags=0,
            )
        for dirs, flags in (([pathlib.Path("relative")], re.UNICODE),
                            ([pathlib.Path("/tmp/evidence")], -1)):
            with self.assertRaises(pydantic.ValidationError):
                ViewMetadata(dirs=dirs, smaps_filter=".*",
                             smaps_filter_flags=flags)


if __name__ == "__main__":
    unittest.main()
