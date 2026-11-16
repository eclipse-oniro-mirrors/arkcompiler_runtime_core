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

import contextlib
import io
import json
import os
import pathlib
import re
import sys
import tempfile
import unittest
from typing import Any

from test.mock.device import FakeDevice

from src.cli_helpers import create_view_with_hooks, execute_iterations
from src.evidence import parse_evidence
from src.store import EvidenceStore, ViewStore
from src.view import read_view

import create_view
import execute_flow
import run
import lib


def logging_hook(root: pathlib.Path, name: str = "hook", fail_event: str = "",
                 edit: bool = False) -> pathlib.Path:
    path = root / name
    path.write_text(f"#!{sys.executable}\n" +
                    "import json, pathlib, sys\n"
                    "path = pathlib.Path(sys.argv[1])\n"
                    "context = json.loads(path.read_text())\n"
                    f"with pathlib.Path({str(root / 'trace')!r}).open('a') as stream:\n"
                    "    stream.write(json.dumps({'context': context, 'cwd': str(pathlib.Path.cwd()),\n"
                    "        'path': str(path), 'exists': pathlib.Path(context['dst']).exists()}) + '\\n')\n"
                    + ("path.write_text('{}')\n" if edit else "") +
                    f"sys.exit(7 if context['event'] == {fail_event!r} else 0)\n",
                    encoding="utf-8")
    path.chmod(0o755)
    return path


def trace(root: pathlib.Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in (root / "trace").read_text().splitlines()]


class OperationHooksTest(unittest.TestCase):
    def test_parser_options_are_phase_specific_ordered_and_repeatable(self) -> None:
        for parser, base, operations in (
            (run.build_parser(), ["--flow", "flow.json"],
             ["execute-flow", "create-view"]),
            (execute_flow.build_parser(), [
             "--flow", "flow.json"], ["execute-flow"]),
            (create_view.build_parser(), [
             "evidence", "--out-dir", "views"], ["create-view"]),
        ):
            for operation in operations:
                for phase in ("pre", "post"):
                    flag = f"--{phase}-{operation}-hook"
                    attribute = flag[2:].replace("-", "_")
                    self.assertEqual(
                        getattr(parser.parse_args(base), attribute), [])
                    parsed_args = parser.parse_args(
                        base + [flag, "a", flag, "b", flag, "a"])
                    self.assertEqual(getattr(parsed_args, attribute), list(
                        map(pathlib.Path, ["a", "b", "a"])))
        with contextlib.redirect_stderr(io.StringIO()):
            for parser, args in (
                (execute_flow.build_parser(), [
                 "--flow", "f", "--pre-create-view-hook", "x"]),
                (create_view.build_parser(), [
                 "e", "--out-dir", "v", "--post-execute-flow-hook", "x"]),
                (run.build_parser(), [
                 "--flow", "f", "--pre-execute-flow-h", "x"]),
            ):
                with self.assertRaises(SystemExit):
                    parser.parse_args(args)

    def test_repeats_and_views_have_ordered_hooks_and_self_contained_outputs(self) -> None:
        cwd = pathlib.Path.cwd()
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            hook = logging_hook(root)
            flow = lib.flow([], desc="canonical")
            sources = execute_iterations(flow, FakeDevice(), root / "out", 2,
                                         reboot=False, device_log=False,
                                         pre_hooks=[hook, hook], post_hooks=[hook])
            self.assertEqual(pathlib.Path.cwd(), cwd)
            for index, source in enumerate(sources):
                create_view_with_hooks([source], root / f"view-{index}",
                                       smaps_filter=re.compile(".*", re.I), plots=False,
                                       pre_hooks=[hook], post_hooks=[hook])
            create_view_with_hooks(sources, root / "combined", smaps_filter=re.compile(".*"),
                                   plots=False, pre_hooks=[hook], post_hooks=[hook])
            entries = trace(root)
            expected = ["pre_execute_flow",
                        "pre_execute_flow", "post_execute_flow"] * 2
            expected += ["pre_create_view", "post_create_view"] * 3
            self.assertEqual([entry["context"]["event"]
                             for entry in entries], expected)
            for entry in entries:
                self.assertEqual(entry["cwd"], str(cwd))
                self.assertFalse(pathlib.Path(entry["path"]).exists())
                self.assertEqual(
                    entry["exists"], entry["context"]["event"].startswith("post_"))
            self.assertEqual(entries[-1]["context"]
                             ["dirs"], list(map(str, sources)))
            for source in sources:
                parsed = parse_evidence(
                    EvidenceStore(source), re.compile(".*"))
                self.assertEqual(parsed.flow, flow)
                self.assertFalse(parsed.metadata.device_log)
            self.assertFalse((root / "out" / "flow.json").exists())
            self.assertEqual(set(read_view(ViewStore(root / "combined")).metadata.model_dump()),
                             {"dirs", "smaps_filter", "smaps_filter_flags", "plots"})

    def test_pre_failure_prevents_collection_and_post_failure_stops_repeats(self) -> None:
        for event in ("pre_execute_flow", "post_execute_flow"):
            with tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                failing = logging_hook(root, "fail", event)
                later = logging_hook(root, "later")
                with self.assertRaisesRegex(RuntimeError, "exit code 7"):
                    execute_iterations(lib.flow([]), FakeDevice(), root / "out", 2,
                                       reboot=False, device_log=False,
                                       pre_hooks=[failing, later] if event.startswith(
                                           "pre") else [],
                                       post_hooks=[failing, later] if event.startswith("post") else [])
                self.assertEqual(len(trace(root)), 1)
                self.assertEqual(
                    (root / "out/iteration_0/evidence").exists(), event.startswith("post"))
                self.assertFalse((root / "out/iteration_1/evidence").exists())

    def test_execution_hook_precedes_reboot_and_failure_skips_post(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            hook = logging_hook(root)

            class FailingReboot(FakeDevice):
                def reboot(self) -> None:
                    if len(trace(root)) != 1:
                        raise AssertionError("pre hook did not precede reboot")
                    raise RuntimeError("reboot failed")

            with self.assertRaisesRegex(RuntimeError, "reboot failed"):
                execute_iterations(lib.flow([]), FailingReboot(), root / "out", 1,
                                   reboot=True, device_log=False, pre_hooks=[hook], post_hooks=[hook])
            self.assertEqual(len(trace(root)), 1)
            self.assertTrue(
                (root / "out/iteration_0/evidence/metadata.json").is_file())

    def test_view_failures_skip_post_and_preserve_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            hook = logging_hook(root)
            with self.assertRaises(RuntimeError):
                create_view_with_hooks([root / "missing"], root / "view",
                                       smaps_filter=re.compile(".*"), plots=False,
                                       pre_hooks=[hook], post_hooks=[hook])
            self.assertEqual(len(trace(root)), 1)
            self.assertFalse((root / "view").exists())
            evidence = lib.execute_flow(
                lib.flow([]), FakeDevice(), root / "evidence", device_log=False)
            failing = logging_hook(root, "fail", "post_create_view")
            with self.assertRaisesRegex(RuntimeError, "exit code 7"):
                create_view_with_hooks([evidence], root / "view",
                                       smaps_filter=re.compile(".*"), plots=False, pre_hooks=[],
                                       post_hooks=[failing, hook])
            read_view(ViewStore(root / "view"))
            self.assertEqual(len(trace(root)), 2)

    def test_relative_paths_and_context_edits_do_not_change_operation(self) -> None:
        cwd = pathlib.Path.cwd()
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            hook = logging_hook(root, edit=True)
            destination = root / "iteration_0" / "evidence"
            results = execute_iterations(
                lib.flow([]), FakeDevice(), pathlib.Path(
                    os.path.relpath(root, cwd)), 1,
                reboot=False, device_log=False,
                pre_hooks=[pathlib.Path(os.path.relpath(hook, cwd))], post_hooks=[hook])
            self.assertEqual(results, [destination])
            self.assertEqual(trace(root)[1]["context"]
                             ["dst"], str(destination))
            self.assertEqual(trace(root)[1]["context"]
                             ["event"], "post_execute_flow")
            create_view_with_hooks([pathlib.Path(os.path.relpath(destination, cwd))],
                                   root / "view", smaps_filter=re.compile(".*"), plots=False,
                                   pre_hooks=[hook], post_hooks=[hook])
            self.assertEqual(
                trace(root)[-1]["context"]["dirs"], [str(destination)])

    def test_all_evidence_destinations_are_checked_before_hooks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "iteration_1/evidence").mkdir(parents=True)
            hook = logging_hook(root)
            with self.assertRaises(FileExistsError):
                execute_iterations(lib.flow([]), FakeDevice(), root, 2,
                                   reboot=False, device_log=False, pre_hooks=[hook], post_hooks=[])
            self.assertFalse((root / "trace").exists())
            self.assertFalse((root / "iteration_0/evidence").exists())


if __name__ == "__main__":
    unittest.main()
