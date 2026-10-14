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

import pydantic

from src.evidence import is_evidence_equivalent, parse_evidence, validate_evidence
from src.metadata import AppMetadata, AppMetadataFile, ArtifactMetadata, ArtifactMetadataFile
from src.store import EvidenceStore
from src.store import ViewStore
from src.view import is_view_equivalent, read_view

import lib


def _write(model: pydantic.BaseModel, path: pathlib.Path) -> None:
    path.write_text(model.model_dump_json(), encoding="utf-8")


def _store(root: pathlib.Path) -> EvidenceStore:
    store = EvidenceStore(root)
    store.local_root().mkdir()
    store.local_snapshots_dir().mkdir()
    store.local_screenshots_dir().mkdir()
    _write(AppMetadataFile(apps=[AppMetadata(pid=1, label="App",
                                             bundle="bundle", ability="ability")]), store.local_app_metadata_path())
    _write(ArtifactMetadataFile(artifacts=[]),
           store.local_artifact_metadata_path(store.local_snapshots_dir()))
    _write(ArtifactMetadataFile(artifacts=[]),
           store.local_artifact_metadata_path(store.local_screenshots_dir()))
    return store


class EvidenceTest(unittest.TestCase):
    def test_empty_evidence_is_valid_without_parent_flow(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            store = EvidenceStore(pathlib.Path(tmp))
            store.local_root().mkdir(exist_ok=True)
            store.local_snapshots_dir().mkdir()
            store.local_screenshots_dir().mkdir()
            _write(AppMetadataFile(apps=[]), store.local_app_metadata_path())
            _write(ArtifactMetadataFile(artifacts=[]),
                   store.local_artifact_metadata_path(store.local_snapshots_dir()))
            _write(ArtifactMetadataFile(artifacts=[]),
                   store.local_artifact_metadata_path(store.local_screenshots_dir()))
            validate_evidence(store)

    def test_missing_eager_directory_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            for missing in ("snapshots", "screenshots"):
                root = pathlib.Path(tmp).joinpath(missing)
                store = EvidenceStore(root)
                store.local_root().mkdir()
                _write(AppMetadataFile(apps=[]),
                       store.local_app_metadata_path())
                existing = (store.local_screenshots_dir() if missing == "snapshots"
                            else store.local_snapshots_dir())
                existing.mkdir()
                _write(ArtifactMetadataFile(artifacts=[]),
                       store.local_artifact_metadata_path(existing))
                with self.assertRaisesRegex(RuntimeError, "missing evidence"):
                    validate_evidence(store)

    def test_missing_metadata_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            for missing in ("apps", "snapshots", "screenshots"):
                root = pathlib.Path(tmp).joinpath(missing)
                store = _store(root)
                if missing == "apps":
                    store.local_app_metadata_path().unlink()
                else:
                    store.local_artifact_metadata_path(
                        getattr(store, f"local_{missing}_dir")()).unlink()
                with self.assertRaisesRegex(RuntimeError, "missing evidence metadata"):
                    validate_evidence(store)

    def test_invalid_snapshot_paths_and_unknown_apps_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            for name, file in (
                ("wrong-parent", "other/App.smaps"),
                ("wrong-extension", "start/App.png"),
                ("unknown-app", "start/Other.smaps"),
            ):
                store = _store(pathlib.Path(tmp).joinpath(name))
                _write(ArtifactMetadataFile(artifacts=[ArtifactMetadata(
                    label="start", timestamp="1", files=[file])]),
                    store.local_artifact_metadata_path(store.local_snapshots_dir()))
                with self.assertRaisesRegex(RuntimeError, "invalid artifact path"):
                    validate_evidence(store)

    def test_screenshots_require_declared_files_and_ignore_extras_and_logs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            store = _store(pathlib.Path(tmp).joinpath("evidence"))
            snapshot = ArtifactMetadata(label="start", timestamp="1",
                                        files=["start/App.smaps"])
            screenshot = ArtifactMetadata(label="screen", timestamp="2",
                                          files=["screen.png"])
            _write(ArtifactMetadataFile(artifacts=[snapshot]),
                   store.local_artifact_metadata_path(store.local_snapshots_dir()))
            _write(ArtifactMetadataFile(artifacts=[screenshot]),
                   store.local_artifact_metadata_path(store.local_screenshots_dir()))
            store.local_snapshot_path("start", "App").parent.mkdir()
            store.local_snapshot_path("start", "App").write_text(
                "declared", encoding="utf-8")
            store.local_screenshot_path("screen").write_text(
                "declared", encoding="utf-8")
            store.local_snapshots_dir().joinpath(
                "ignored.smaps").write_text("extra", encoding="utf-8")
            store.local_screenshots_dir().joinpath(
                "ignored.png").write_text("extra", encoding="utf-8")
            store.local_logs_dir().mkdir()
            store.local_log_path().write_text("ignored", encoding="utf-8")
            validate_evidence(store)

            store.local_screenshot_path("screen").unlink()
            with self.assertRaisesRegex(RuntimeError, "missing declared evidence file"):
                validate_evidence(store)

    def test_invalid_screenshot_paths_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            for name, file in (("wrong-parent", "screen/screen.png"),
                               ("wrong-extension", "screen.smaps")):
                store = _store(pathlib.Path(tmp).joinpath(name))
                _write(ArtifactMetadataFile(artifacts=[ArtifactMetadata(
                    label="screen", timestamp="1", files=[file])]),
                    store.local_artifact_metadata_path(store.local_screenshots_dir()))
                with self.assertRaisesRegex(RuntimeError, "invalid artifact path"):
                    validate_evidence(store)

    def test_declared_snapshot_file_must_exist(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            store = _store(pathlib.Path(tmp).joinpath("evidence"))
            _write(ArtifactMetadataFile(artifacts=[ArtifactMetadata(
                label="start", timestamp="1", files=["start/App.smaps"])]),
                store.local_artifact_metadata_path(store.local_snapshots_dir()))
            with self.assertRaisesRegex(RuntimeError, "missing declared evidence file"):
                validate_evidence(store)

    def test_parse_uses_declared_files_and_equivalence_ignores_pid(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            parsed = []
            for name, pid in (("one", 1), ("two", 2)):
                store = EvidenceStore(root.joinpath(name))
                store.local_root().mkdir()
                store.local_snapshots_dir().mkdir()
                store.local_screenshots_dir().mkdir()
                _write(AppMetadataFile(apps=[AppMetadata(pid=pid, label="App",
                                                         bundle="bundle", ability="ability")]),
                       store.local_app_metadata_path())
                artifact = ArtifactMetadata(label="start", timestamp="1",
                                            files=["start/App.smaps"])
                _write(ArtifactMetadataFile(artifacts=[artifact]),
                       store.local_artifact_metadata_path(store.local_snapshots_dir()))
                _write(ArtifactMetadataFile(artifacts=[]),
                       store.local_artifact_metadata_path(store.local_screenshots_dir()))
                store.local_snapshot_path("start", "App").parent.mkdir()
                store.local_snapshot_path("start", "App").write_text(
                    "1-2 rw-p 0 00:00 0 [anon]\nSize: 1 kB\n", encoding="utf-8")
                store.local_snapshots_dir().joinpath(
                    "ignored.smaps").write_text("", encoding="utf-8")
                parsed.append(parse_evidence(store, re.compile(".*")))
            self.assertEqual(len(parsed[0].measurements), 1)
            self.assertTrue(is_evidence_equivalent(parsed[0], parsed[1]))

    def test_create_view_writes_uniform_aggregate_layout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            evidence = root.joinpath("evidence")
            store = EvidenceStore(evidence)
            evidence.mkdir()
            store.local_snapshots_dir().mkdir()
            store.local_screenshots_dir().mkdir()
            _write(AppMetadataFile(apps=[AppMetadata(pid=1, label="App",
                                                     bundle="bundle", ability="ability")]), store.local_app_metadata_path())
            artifact = ArtifactMetadata(label="start", timestamp="1",
                                        files=["start/App.smaps"])
            _write(ArtifactMetadataFile(artifacts=[artifact]),
                   store.local_artifact_metadata_path(store.local_snapshots_dir()))
            _write(ArtifactMetadataFile(artifacts=[]),
                   store.local_artifact_metadata_path(store.local_screenshots_dir()))
            store.local_snapshot_path("start", "App").parent.mkdir()
            store.local_snapshot_path("start", "App").write_text(
                "1-2 rw-p 0 00:00 0 [anon]\nSize: 1 kB\n", encoding="utf-8")
            view = lib.create_view([evidence], root.joinpath("view"))
            self.assertTrue(view.joinpath("summary.csv").is_file())
            self.assertTrue(view.joinpath(
                "breakdowns", "start", "App.csv").is_file())
            self.assertTrue(view.joinpath("plots").is_dir())
            parsed = read_view(ViewStore(view))
            self.assertTrue(is_view_equivalent(parsed, parsed))
            original_smaps = store.local_snapshot_path("start", "App").read_text(
                encoding="utf-8")
            original_summary = view.joinpath(
                "summary.csv").read_text(encoding="utf-8")
            empty_view = lib.create_view([evidence], root.joinpath("empty-view"),
                                         re.compile("^never$"))
            with self.assertRaises(FileExistsError):
                lib.create_view([evidence], view)
            self.assertTrue(empty_view.joinpath("plots").is_dir())
            self.assertEqual(store.local_snapshot_path("start", "App").read_text(
                encoding="utf-8"), original_smaps)
            self.assertEqual(view.joinpath("summary.csv").read_text(
                encoding="utf-8"), original_summary)

    def test_create_view_warns_for_each_unmatched_declared_smaps_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            evidence = root.joinpath("evidence")
            store = EvidenceStore(evidence)
            evidence.mkdir()
            store.local_snapshots_dir().mkdir()
            store.local_screenshots_dir().mkdir()
            _write(AppMetadataFile(apps=[AppMetadata(pid=1, label="App",
                                                     bundle="bundle", ability="ability")]), store.local_app_metadata_path())
            artifact = ArtifactMetadata(label="start", timestamp="1",
                                        files=["start/App.smaps"])
            _write(ArtifactMetadataFile(artifacts=[artifact]),
                   store.local_artifact_metadata_path(store.local_snapshots_dir()))
            _write(ArtifactMetadataFile(artifacts=[]),
                   store.local_artifact_metadata_path(store.local_screenshots_dir()))
            store.local_snapshot_path("start", "App").parent.mkdir()
            store.local_snapshot_path(
                "start", "App").write_text("", encoding="utf-8")
            with mock.patch("lib.get_logger") as logger:
                lib.create_view([evidence], root.joinpath("view"))
                lib.create_view([evidence], root.joinpath("second-view"))
            logger.return_value.warn.assert_has_calls([
                mock.call(f"smaps file {store.local_snapshot_path('start', 'App')} "
                          "has no mappings aggregated for pattern .*"),
                mock.call(f"smaps file {store.local_snapshot_path('start', 'App')} "
                          "has no mappings aggregated for pattern .*"),
            ])
            self.assertEqual(logger.return_value.warn.call_count, 2)

    def test_create_view_does_not_warn_for_matching_zero_profile(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            evidence = root.joinpath("evidence")
            store = EvidenceStore(evidence)
            evidence.mkdir()
            store.local_snapshots_dir().mkdir()
            store.local_screenshots_dir().mkdir()
            _write(AppMetadataFile(apps=[AppMetadata(pid=1, label="App",
                                                     bundle="bundle", ability="ability")]), store.local_app_metadata_path())
            artifact = ArtifactMetadata(label="start", timestamp="1",
                                        files=["start/App.smaps"])
            _write(ArtifactMetadataFile(artifacts=[artifact]),
                   store.local_artifact_metadata_path(store.local_snapshots_dir()))
            _write(ArtifactMetadataFile(artifacts=[]),
                   store.local_artifact_metadata_path(store.local_screenshots_dir()))
            store.local_snapshot_path("start", "App").parent.mkdir()
            store.local_snapshot_path("start", "App").write_text(
                "1-2 rw-p 0 00:00 0 [anon]\n", encoding="utf-8")
            with mock.patch("lib.get_logger") as logger:
                lib.create_view([evidence], root.joinpath("view"))
            logger.return_value.warn.assert_not_called()


if __name__ == "__main__":
    unittest.main()
