#!/usr/bin/env python3
# -- coding: utf-8 --
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
from pathlib import Path
from unittest import TestCase
from unittest.mock import patch

from runner.extensions.generators.sts_stdlib.func_templates_generator import FuncTestsCodeGenerator
from runner.options.config import Config
from runner.test.generators_test.data import data_test_suite0
from runner.test.test_utils import random_suffix


class FuncTemplatesGeneratorTest(TestCase):
    args = data_test_suite0.args

    def setUp(self) -> None:
        os.environ["ARKCOMPILER_RUNTIME_CORE_PATH"] = str(Path(".").resolve())
        os.environ["ARKCOMPILER_ETS_FRONTEND_PATH"] = str(Path(".").resolve())
        os.environ["WORK_DIR"] = str(Path(".").resolve())
        os.environ["PANDA_BUILD"] = str(Path(".").resolve())
        self.config = Config(self.args)

    @patch('runner.options.local_env.LocalEnv.get_instance_id', lambda: "111011111")
    def test_generate(self) -> None:
        # preparation
        test_source_path: Path = Path(__file__).with_name("data")
        test_gen_path: Path = Path(__file__).with_name("gen")
        shutil.rmtree(test_gen_path, ignore_errors=True)
        # test
        ets_templates_generator = FuncTestsCodeGenerator(test_source_path, test_gen_path, self.config)
        ets_templates_generator.generate()
        self.assertTrue(test_gen_path.exists())
        generated_tests = sorted(test.stem for test in test_gen_path.glob("*"))
        expected_tests = [
            "test-func-0001",
            "test-func-0002",
            "test-func-0003",
            "test-func-0004"
        ]
        self.assertEqual(len(generated_tests), 4)
        self.assertListEqual(generated_tests, expected_tests)
        # clear up
        shutil.rmtree(test_gen_path, ignore_errors=True)

    def test_generate_empty(self) -> None:
        # preparation
        test_source_path: Path = Path(__file__).with_name("data" + random_suffix())
        test_gen_path: Path = Path(__file__).with_name("gen" + random_suffix())
        test_source_path.mkdir(parents=True, exist_ok=True)
        shutil.rmtree(test_gen_path, ignore_errors=True)
        # test
        ets_templates_generator = FuncTestsCodeGenerator(test_source_path, test_gen_path, self.config)
        ets_templates_generator.generate()
        self.assertTrue(test_gen_path.exists())
        generated_tests = sorted(test.stem for test in test_gen_path.glob("*"))
        expected_tests: list[str] = []
        self.assertEqual(len(generated_tests), 0)
        self.assertListEqual(generated_tests, expected_tests)
        # clear up
        shutil.rmtree(test_source_path, ignore_errors=True)
        shutil.rmtree(test_gen_path, ignore_errors=True)
