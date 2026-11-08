#!/usr/bin/env python3
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

"""Check acceptance selection and outcomes without starting the VM or compiler."""

import contextlib
import fnmatch
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


SCRIPT = Path(__file__).resolve().parents[2] / "scripts/run_thread_safety_acceptance.py"
SPEC = importlib.util.spec_from_file_location("thread_safety_acceptance", SCRIPT)
ACCEPTANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ACCEPTANCE)


class ThreadSafetyAcceptanceTest(unittest.TestCase):
    def run_focused(self, outcomes):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / "out/test"
            build.mkdir(parents=True)
            (build / "args.gn").write_text("is_tsan = false\n")
            destination = Path(directory) / "evidence"

            def run_tests(command, **_kwargs):
                failures = outcomes.count("failure")
                root = ET.Element("testsuites", tests=str(len(outcomes)), failures=str(failures), errors="0")
                suite = ET.SubElement(root, "testsuite", name="SelectedTest")
                for index, outcome in enumerate(outcomes):
                    case = ET.SubElement(suite, "testcase", name=f"Case{index}", status="run",
                                         result="skipped" if outcome == "skipped" else "completed")
                    if outcome in ("failure", "skipped"):
                        ET.SubElement(case, outcome)
                xml_path = next(value.split("xml:", 1)[1] for value in command
                                if value.startswith("--gtest_output="))
                ET.ElementTree(root).write(xml_path)
                return subprocess.CompletedProcess(command, int(failures != 0))

            args = [str(SCRIPT), "--build-dir", str(build), "--evidence-dir", str(destination),
                    "--filter", "SelectedTest.*"]
            with patch.object(sys, "argv", args), \
                    patch.object(ACCEPTANCE.subprocess, "check_output", return_value=""), \
                    patch.object(ACCEPTANCE.subprocess, "run", side_effect=run_tests), \
                    patch.object(ACCEPTANCE, "digest", return_value="test-digest"), \
                    contextlib.redirect_stdout(io.StringIO()):
                code = ACCEPTANCE.main()
            return code, json.loads((destination / "summary.json").read_text())

    def test_completed_selection_passes(self):
        code, report = self.run_focused(["completed"])
        self.assertEqual(code, 0)
        self.assertTrue(report["valid"])
        self.assertEqual(report["passed"], 1)

    def test_empty_selection_fails(self):
        code, report = self.run_focused([])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["valid"])

    def test_failed_selection_fails(self):
        code, report = self.run_focused(["failure"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["valid"])

    def test_all_skipped_selection_fails(self):
        code, report = self.run_focused(["skipped"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["valid"])

    def test_partially_skipped_selection_fails(self):
        code, report = self.run_focused(["completed", "skipped"])
        self.assertNotEqual(code, 0)
        self.assertFalse(report["valid"])

    def test_tsan_excludes_unconditional_vm_cases(self):
        positive, negative = ACCEPTANCE.TSAN.split("-", 1)

        def selected(name):
            return (any(fnmatch.fnmatchcase(name, pattern) for pattern in positive.split(":"))
                    and not any(fnmatch.fnmatchcase(name, pattern) for pattern in negative.split(":")))

        vm_cases = [
            "LibAbcKitGraphIsaIntegrationTest.DynamicGraphIsaSingleThreadRegression",
            "LibAbcKitDynamicThreadSafetyTest."
            "DynamicPreparedFunctionPersistsIsaModificationAfterGraphDestroyedExecution",
        ]
        vm_cases.extend("LibAbcKitDynamicScenarioParallelTest.LibAbcKitTestDynamic" + scenario + "SerialParallel"
                        for scenario in ("AddLog", "AddTryCatch", "BranchEliminator", "ParameterCheck",
                                         "ReplaceCallSite"))
        for name in vm_cases:
            with self.subTest(name=name):
                self.assertFalse(selected(name))
        self.assertTrue(selected("LibAbcKitDynamicGraphVersionTest.SameGraphCanCommitSuccessiveEdits"))
        self.assertTrue(selected("AbckitParallelAddLogScenarioTest.LibAbcKitTestParallelAddLogDynamic"))
        self.assertTrue(selected("AbckitBranchEliminatorFailureTest.RunStopsAfterTaskFailure"))


if __name__ == "__main__":
    unittest.main()
