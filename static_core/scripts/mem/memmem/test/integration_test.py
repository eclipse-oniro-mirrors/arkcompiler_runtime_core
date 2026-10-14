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

import hashlib
import pathlib
import re
import shutil
import tempfile
import unittest

from test.mock.device import FakeDevice

from compare import compare_reports
from src.store import ViewStore
from src.view import read_view

import lib


SMAPS = "1-2 rw-p 0 00:00 0 /system/lib64/libexample.so\nSize: 4 kB\nRss: 2 kB\nPss: 1 kB\n"


class CollectionViewComparisonIntegrationTest(unittest.TestCase):
    def test_fake_device_collection_views_reading_and_comparison(self) -> None:
        flow = lib.flow([lib.app_flow(
            "App", "com.example", "EntryAbility", False,
            [lib.snapshot("start"), lib.screenshot("screen")],
        )])
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            first = self._collect(flow, root / "first")
            second = self._collect(flow, root / "second")
            first.joinpath("snapshots", "extra.smaps").write_text(
                SMAPS, encoding="utf-8")
            first.joinpath("logs").mkdir()
            first.joinpath("logs", "extra.log").write_text(
                "extra", encoding="utf-8")
            evidence_before = self._tree_digest(first)

            # Declared files alone determine input; extras and logs are ignored.
            single = lib.create_view([first], root / "single")
            combined = lib.create_view([first, second], root / "combined")
            empty = lib.create_view(
                [first], root / "empty", re.compile("^nomatch$"))
            self.assertEqual(read_view(ViewStore(single)
                                       ).summary_rows[0]["n_samples"], "1")
            self.assertEqual(read_view(ViewStore(combined)
                                       ).summary_rows[0]["n_samples"], "2")
            self.assertEqual(read_view(ViewStore(empty)).summary_rows, [])
            self.assertTrue(empty.joinpath("plots").is_dir())
            self.assertTrue(empty.joinpath("breakdowns").is_dir())

            missing = root / "missing"
            shutil.copytree(first, missing)
            missing.joinpath("snapshots", "start", "App.smaps").unlink()
            with self.assertRaisesRegex(RuntimeError, "missing declared evidence file"):
                lib.create_view([missing], root / "invalid")

            comparison = root / "comparison"
            compare_reports(single, [combined], comparison)
            self.assertTrue(comparison.joinpath("summary.csv").is_file())
            self.assertEqual(self._tree_digest(first), evidence_before)

    def _collect(self, flow: lib.Flow, destination: pathlib.Path) -> pathlib.Path:
        return lib.execute_flow(
            flow, FakeDevice(default_smaps_content=SMAPS), destination,
            device_log=False)

    def _tree_digest(self, root: pathlib.Path) -> dict[pathlib.Path, str]:
        return {
            path.relative_to(root): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob("*") if path.is_file()
        }


if __name__ == "__main__":
    unittest.main()
