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

from src.metadata import RelativeParts


class EvidenceStore:
    def __init__(
        self,
        local_root: pathlib.Path,
        remote_root: pathlib.PurePosixPath | None = None,
    ) -> None:
        self._local_root = local_root
        self._remote_root = remote_root

    def local_root(self) -> pathlib.Path:
        return self._local_root

    def local_app_metadata_path(self) -> pathlib.Path:
        return self._local_root.joinpath("apps.json")

    def local_artifact_metadata_path(self, category: pathlib.Path) -> pathlib.Path:
        return category.joinpath("metadata.json")

    def local_snapshots_dir(self) -> pathlib.Path:
        return self._local_root.joinpath("snapshots")

    def local_snapshot_path(self, snapshot_label: str, app_label: str) -> pathlib.Path:
        return self.local_snapshots_dir().joinpath(snapshot_label, f"{app_label}.smaps")

    def snapshot_relative_parts(self, snapshot_label: str, app_label: str) -> RelativeParts:
        return [snapshot_label, f"{app_label}.smaps"]

    def local_screenshots_dir(self) -> pathlib.Path:
        return self._local_root.joinpath("screenshots")

    def local_screenshot_path(self, screenshot_label: str) -> pathlib.Path:
        return self.local_screenshots_dir().joinpath(f"{screenshot_label}.png")

    def screenshot_relative_parts(self, screenshot_label: str) -> RelativeParts:
        return [f"{screenshot_label}.png"]

    def local_logs_dir(self) -> pathlib.Path:
        return self._local_root.joinpath("logs")

    def local_log_path(self) -> pathlib.Path:
        return self.local_logs_dir().joinpath("log.log")

    def log_relative_parts(self) -> RelativeParts:
        return ["log.log"]

    def remote_root(self) -> pathlib.PurePosixPath:
        return self._remote()

    def remote_snapshots_dir(self) -> pathlib.PurePosixPath:
        return self._remote().joinpath("snapshots")

    def remote_snapshot_dir(self, snapshot_label: str) -> pathlib.PurePosixPath:
        return self.remote_snapshots_dir().joinpath(snapshot_label)

    def remote_snapshot_path(
        self, snapshot_label: str, app_label: str
    ) -> pathlib.PurePosixPath:
        return self.remote_snapshot_dir(snapshot_label).joinpath(f"{app_label}.smaps")

    def remote_screenshots_dir(self) -> pathlib.PurePosixPath:
        return self._remote().joinpath("screenshots")

    def remote_screenshot_path(self, screenshot_label: str) -> pathlib.PurePosixPath:
        return self.remote_screenshots_dir().joinpath(f"{screenshot_label}.png")

    def remote_logs_dir(self) -> pathlib.PurePosixPath:
        return self._remote().joinpath("logs")

    def remote_log_path(self) -> pathlib.PurePosixPath:
        return self.remote_logs_dir().joinpath("log.log")

    def _remote(self) -> pathlib.PurePosixPath:
        if self._remote_root is None:
            raise RuntimeError("evidence store has no remote root")
        return self._remote_root


class ViewStore:
    def __init__(self, root: pathlib.Path) -> None:
        self._root = root

    def local_root(self) -> pathlib.Path:
        return self._root

    def local_metadata_path(self) -> pathlib.Path:
        return self._root.joinpath("metadata.json")

    def local_summary_path(self) -> pathlib.Path:
        return self._root.joinpath("summary.csv")

    def local_outliers_path(self) -> pathlib.Path:
        return self._root.joinpath("outliers.csv")

    def local_breakdowns_dir(self) -> pathlib.Path:
        return self._root.joinpath("breakdowns")

    def local_breakdown_metadata_path(self) -> pathlib.Path:
        return self.local_breakdowns_dir().joinpath("metadata.json")

    def local_breakdown_path(self, snapshot_label: str, app_label: str) -> pathlib.Path:
        return self.local_breakdowns_dir().joinpath(snapshot_label, f"{app_label}.csv")

    def local_plots_dir(self) -> pathlib.Path:
        return self._root.joinpath("plots")

    def local_plot_path(self, app_label: str, metric: str) -> pathlib.Path:
        return self.local_plots_dir().joinpath(
            app_label, f"{app_label}-{metric}.svg")
