#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2025 Huawei Device Co., Ltd.
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
import shutil
import unittest
from pathlib import Path
from typing import cast

from runner.options.cli_options import CliOptions
from runner.test.config_test import data_collections
from runner.test.test_utils import compare_dicts


class TestSuiteConfigTest3(unittest.TestCase):
    cfg_ext = ".yaml"
    workflow_name = "config-1"
    workflow_path: Path
    test_suite_name = "test_suite3"
    test_suite_path: Path
    current_path = Path(__file__).parent
    cfg_path = current_path.parent.parent.parent.joinpath("cfg")
    collections = data_collections.collections

    @classmethod
    def setUpClass(cls) -> None:
        os.environ["PANDA_SOURCE_PATH"] = "."
        os.environ["WORK_DIR"] = "."
        os.environ["PANDA_BUILD"] = "."

        shutil.copy(cls.current_path.joinpath(cls.workflow_name + cls.cfg_ext), cls.cfg_path.joinpath("workflows"))
        cls.workflow_path = cls.cfg_path.joinpath("workflows").joinpath(cls.workflow_name + cls.cfg_ext)

        shutil.copy(cls.current_path.joinpath(cls.test_suite_name + cls.cfg_ext), cls.cfg_path.joinpath("test-suites"))
        cls.test_suite_path = cls.cfg_path.joinpath("test-suites").joinpath(cls.test_suite_name + cls.cfg_ext)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.workflow_path.unlink(missing_ok=True)
        cls.test_suite_path.unlink(missing_ok=True)

    def test_collections1(self) -> None:
        args = [self.workflow_name, self.test_suite_name]
        actual = CliOptions(args)
        actual_data = cast(dict, actual.data.get("test_suite3.data"))
        self.assertIn("collections", actual_data)
        actual_collections = cast(dict, actual_data["collections"])
        compare_dicts(self, actual_collections, self.collections)
