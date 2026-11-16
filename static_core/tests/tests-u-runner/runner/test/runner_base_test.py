#!/usr/bin/env python3
# -*- coding: utf-8 -*-
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

"""Regression tests for source selection across multiple file extensions."""

from pathlib import Path
from shutil import rmtree
from tempfile import mkdtemp
from types import MethodType
from unittest import TestCase
from unittest.mock import Mock

from runner.enum_types.test_directory import TestDirectory
from runner.runner_base import Runner, load_list
from runner.test_base import Test


class RunnerSourceSelectionTest(TestCase):
    def setUp(self) -> None:
        self.root = Path(mkdtemp())
        self.addCleanup(rmtree, self.root)
        category = self.root / "suggest_final_tests"
        category.mkdir()
        for extension in ("ets", "ats"):
            (category / f"case.{extension}").write_text("function main(): void {}", encoding="utf-8")
        self.runner = Mock(spec=Runner)
        self.runner.test_root = self.root
        self.runner.tests = set()
        self.runner.ignored_tests = set()
        self.runner.excluded_tests = set()
        self.runner.explicit_test = None
        self.runner.explicit_list = None
        self.runner.update_excluded = False
        self.runner.config = Mock()
        self.runner.config.test_lists.groups.quantity = 1
        self.runner.config.test_lists.groups.chapters = None
        self.runner.config.test_lists.filter = "*"
        self.runner.config.test_lists.skip_test_lists = True
        self.runner.create_test.side_effect = lambda file, flags, ignored: Test(
            Mock(), file, flags, str(Path(file).relative_to(self.root)))
        self.runner.load_tests_from_lists.side_effect = lambda lists, directory: load_list(
            str(self.root), lists[0], directory)
        discover = getattr(Runner, "_Runner__load_test_files")
        setattr(self.runner, "_Runner__load_test_files", MethodType(discover, self.runner))

    def test_explicit_file_registered_once(self) -> None:
        for extension in ("ets", "ats"):
            with self.subTest(extension=extension):
                self.runner.tests.clear()
                self.runner.create_test.reset_mock()
                self.runner.explicit_test = f"suggest_final_tests/case.{extension}"
                self._add_directories()
                self.runner.create_test.assert_called_once()
                self.assertEqual([test.test_id for test in self.runner.tests],
                                 [self.runner.explicit_test])

    def test_explicit_list_registered_once_per_file(self) -> None:
        selected = self.root / "selected.txt"
        selected.write_text("suggest_final_tests/case.ets\nsuggest_final_tests/case.ats\n", encoding="utf-8")
        self.runner.explicit_list = str(selected)
        self._add_directories()
        self.assertEqual(self.runner.create_test.call_count, 2)
        self.assertEqual({test.test_id for test in self.runner.tests},
                         {f"suggest_final_tests/case.{extension}" for extension in ("ets", "ats")})

    def test_directory_discovery_keeps_both_extensions(self) -> None:
        self._add_directories()
        self.assertEqual(self.runner.create_test.call_count, 2)
        self.assertEqual(len(self.runner.tests), 2)

    def _add_directories(self) -> None:
        for extension in ("ets", "ats"):
            Runner.add_directory(self.runner, TestDirectory("suggest_final_tests", extension, ["--ets-warnings"]))
        for test in self.runner.tests:
            self.assertEqual(test.flags, ["--ets-warnings"])
