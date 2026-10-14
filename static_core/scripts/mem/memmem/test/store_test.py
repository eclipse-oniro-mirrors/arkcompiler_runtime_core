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
import unittest

from src.store import EvidenceStore, ViewStore


_REMOTE_OUT_DIR = pathlib.PurePosixPath(
    "/", "data", "local", "tmp", "memmem-run")
_OUT_DIR = pathlib.Path.cwd().joinpath("out")


def _local(suffix: str) -> pathlib.Path:
    return _OUT_DIR.joinpath(suffix)


def _evidence_store(path: pathlib.Path = _OUT_DIR) -> EvidenceStore:
    return EvidenceStore(path, _REMOTE_OUT_DIR)


class EvidenceStoreTest(unittest.TestCase):
    def test_local_paths(self) -> None:
        store = _evidence_store()

        self.assertEqual(store.local_root(), _OUT_DIR)
        self.assertEqual(store.local_app_metadata_path(),
                         _local("apps.json"))
        self.assertEqual(
            store.local_artifact_metadata_path(store.local_snapshots_dir()),
            _local("snapshots/metadata.json"),
        )
        self.assertEqual(
            store.local_artifact_metadata_path(store.local_screenshots_dir()),
            _local("screenshots/metadata.json"),
        )
        self.assertEqual(store.local_snapshots_dir(), _local("snapshots"))

        self.assertEqual(store.local_logs_dir(), _local("logs"))
        self.assertEqual(store.local_log_path(), _local("logs/log.log"))
        self.assertEqual(store.log_relative_parts(), ["log.log"])
        self.assertEqual(store.local_screenshots_dir(), _local("screenshots"))
        self.assertEqual(store.local_screenshot_path("after_start"),
                         _local("screenshots/after_start.png"))
        self.assertEqual(store.screenshot_relative_parts(
            "after_start"), ["after_start.png"])

        self.assertEqual(store.snapshot_relative_parts(
            "after_start", "App"), ["after_start", "App.smaps"])
        self.assertEqual(
            store.local_snapshot_path("after_start", "App"),
            _local("snapshots/after_start/App.smaps"),
        )

    def test_remote_paths(self) -> None:
        store = _evidence_store()

        self.assertEqual(store.remote_root(), _REMOTE_OUT_DIR)
        self.assertEqual(
            store.remote_logs_dir(),
            pathlib.PurePosixPath("/", "data", "local",
                                  "tmp", "memmem-run", "logs"),
        )
        self.assertEqual(
            store.remote_log_path(),
            pathlib.PurePosixPath(
                "/", "data", "local", "tmp", "memmem-run", "logs", "log.log"),
        )
        self.assertEqual(
            store.remote_screenshots_dir(),
            pathlib.PurePosixPath("/", "data", "local",
                                  "tmp", "memmem-run", "screenshots"),
        )
        self.assertEqual(
            store.remote_screenshot_path("after_start"),
            pathlib.PurePosixPath(
                "/", "data", "local", "tmp", "memmem-run", "screenshots", "after_start.png"),
        )
        self.assertEqual(
            store.remote_snapshots_dir(),
            pathlib.PurePosixPath("/", "data", "local",
                                  "tmp", "memmem-run", "snapshots"),
        )
        self.assertEqual(
            store.remote_snapshot_dir("after_start"),
            pathlib.PurePosixPath(
                "/", "data", "local", "tmp", "memmem-run", "snapshots", "after_start"),
        )
        self.assertEqual(
            store.remote_snapshot_path("after_start", "App"),
            pathlib.PurePosixPath(
                "/", "data", "local", "tmp", "memmem-run", "snapshots", "after_start", "App.smaps"),
        )

    def test_remote_paths_require_remote_root(self) -> None:
        store = EvidenceStore(_OUT_DIR)
        with self.assertRaisesRegex(RuntimeError, "no remote root"):
            store.remote_root()


class ViewStoreTest(unittest.TestCase):
    def test_local_paths(self) -> None:
        store = ViewStore(_OUT_DIR)
        self.assertEqual(store.local_metadata_path(), _local("metadata.json"))
        self.assertEqual(store.local_summary_path(), _local("summary.csv"))
        self.assertEqual(store.local_outliers_path(), _local("outliers.csv"))
        self.assertEqual(store.local_breakdown_metadata_path(),
                         _local("breakdowns/metadata.json"))
        self.assertEqual(store.local_breakdown_path("after_start", "App"),
                         _local("breakdowns/after_start/App.csv"))
        self.assertEqual(store.local_plot_path("App", "rss"),
                         _local("plots/App/App-rss.svg"))


if __name__ == "__main__":
    unittest.main()
