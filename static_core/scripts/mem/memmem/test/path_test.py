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

import os
import pathlib
import tempfile
import unittest

from src.path import absolutize, normalize


class PathTest(unittest.TestCase):
    def test_normalize_collapses_relative_dot_components(self) -> None:
        self.assertEqual(normalize(pathlib.Path("a/./b/../c")),
                         pathlib.Path("a/c"))

    def test_normalize_collapses_absolute_dot_components(self) -> None:
        self.assertEqual(normalize(pathlib.Path("/tmp/a/../output")),
                         pathlib.Path("/tmp/output"))

    def test_absolutize_normalizes_nonexistent_destination(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            old_cwd = pathlib.Path.cwd()
            try:
                os.chdir(tmp)
                self.assertEqual(absolutize(pathlib.Path("out/../new")),
                                 pathlib.Path(tmp, "new"))
            finally:
                os.chdir(old_cwd)

    def test_normalize_preserves_symlink_spelling(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            root.joinpath("actual").mkdir()
            root.joinpath("current").symlink_to(root.joinpath("actual"),
                                                target_is_directory=True)
            path = root.joinpath("current", "..", "current", "evidence")
            self.assertEqual(
                normalize(path), root.joinpath("current", "evidence"))

    def test_absolutize_normalizes_lexical_duplicate_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            old_cwd = pathlib.Path.cwd()
            try:
                os.chdir(tmp)
                self.assertEqual(absolutize(pathlib.Path("evidence")),
                                 absolutize(pathlib.Path("tmp/../evidence")))
            finally:
                os.chdir(old_cwd)


if __name__ == "__main__":
    unittest.main()
