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
import unittest
from pathlib import Path
from typing import ClassVar
from unittest.mock import patch

from runner.options.options import IOptions
from runner.options.options_general import GeneralOptions
from runner.options.options_test_suite import TestSuiteOptions
from runner.options.options_workflow import WorkflowOptions
from runner.options.step import StepKind
from runner.test.config_test.data import data_1


class WorkflowConfigTest(unittest.TestCase):
    current_path = Path(__file__).parent
    data_folder = current_path / "data"
    test_environ: ClassVar[dict[str, str]] = {
        'ARKCOMPILER_RUNTIME_CORE_PATH': Path.cwd().as_posix(),
        'ARKCOMPILER_ETS_FRONTEND_PATH': Path.cwd().as_posix(),
        'PANDA_BUILD': Path.cwd().as_posix(),
        'WORK_DIR': Path.cwd().as_posix()
    }
    args = data_1.args

    def prepare_test(self) -> tuple[TestSuiteOptions, WorkflowOptions]:
        general = GeneralOptions(self.args, IOptions())
        test_suite = TestSuiteOptions(self.args, general)
        workflow = WorkflowOptions(cfg_content=self.args, parent_test_suite=test_suite)
        return test_suite, workflow

    @patch.dict(os.environ, test_environ, clear=True)
    def test_loaded_steps(self) -> None:
        _, workflow = self.prepare_test()
        self.assertEqual(len(workflow.steps), 1)
        self.assertSetEqual(
            {step.name for step in workflow.steps},
            {'echo'}
        )

    @patch.dict(os.environ, test_environ, clear=True)
    def test_echo_step(self) -> None:
        _, workflow = self.prepare_test()
        echo = [step for step in workflow.steps if step.name == 'echo']
        self.assertEqual(len(echo), 1, "Step 'es2panda' not found")
        step = echo[0]
        self.assertEqual(str(step.executable_path), "/usr/bin/echo")
        self.assertEqual(step.step_kind, StepKind.OTHER)
        self.assertEqual(step.timeout, 20)
        self.assertTrue(step.can_be_instrumented)
        self.assertSetEqual(set(step.args), {
            "--arktsconfig=hello",
            "--gen-stdlib=false",
            "--thread=0",
            "--extension=ets",
            "${test-id}"
        })

    @patch.dict(os.environ, test_environ, clear=True)
    def test_test_suite(self) -> None:
        test_suite, _ = self.prepare_test()
        self.assertEqual(test_suite.suite_name, "test_suite1")
        self.assertEqual(test_suite.test_root, Path.cwd().resolve())
        self.assertEqual(test_suite.list_root, Path.cwd().resolve())
        self.assertEqual(test_suite.extension(), "sts")
        self.assertEqual(test_suite.load_runtimes(), "ets")
        self.assertEqual(test_suite.work_dir, ".")
