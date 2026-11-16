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

import json
import pathlib
import re
import subprocess
import sys
import tempfile
import typing
import unittest

from src.hooks import ExecuteHookContext, ViewHookContext, run_hooks
from src.log import LogLevel, configure_logger, reset_logger
from src.schema import Flow


class HooksTest(unittest.TestCase):
    def test_readme_python_and_bash_examples_run(self) -> None:
        readme = pathlib.Path(__file__).parents[1].joinpath(
            "README.md").read_text()
        hooks_section = readme.split("### User hooks", 1)[
            1].split("## Record", 1)[0]
        for language in ("python", "bash"):
            match = re.search(rf"```{language}\n(#!.*?)\n```",
                              hooks_section, flags=re.DOTALL)
            assert match is not None
            with tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                script = root / "hook"
                script.write_text(match.group(1) + "\n", encoding="utf-8")
                script.chmod(0o755)
                context = root / "context.json"
                context.write_text(json.dumps(
                    {"event": "pre_execute_flow", "dst": "/a b"}))
                result = subprocess.run([str(script), str(context)], check=True,
                                        capture_output=True, text=True)
                self.assertEqual(result.stdout.strip(),
                                 "pre_execute_flow: /a b")

    def test_context_schema_and_ordered_duplicate_process_calls(self) -> None:
        cwd = pathlib.Path.cwd()
        contexts = [
            ExecuteHookContext(event=event, flow=Flow(flow=[], desc="description"),
                               dst=cwd / "evidence", reboot=False, device_log=True)
            for event in ("pre_execute_flow", "post_execute_flow")
        ] + [
            ViewHookContext(event=event, dirs=[cwd / "b", cwd / "a"], dst=cwd / "view",
                            smaps_filter_pattern=".*", smaps_filter_flags=32, plots=False)
            for event in ("pre_create_view", "post_create_view")
        ]
        paths = [cwd / "A", cwd / "B", cwd / "A"]

        def make_runner(expected_context: ExecuteHookContext | ViewHookContext,
                        call_list: list[list[str]]) -> typing.Callable[..., None]:
            def runner(args: list[str], *, cwd: pathlib.Path,
                       check: bool, timeout: int) -> None:
                self.assertEqual(cwd, pathlib.Path.cwd())
                self.assertTrue(check)
                self.assertEqual(timeout, 300)
                self.assertEqual(len(args), 2)
                payload = json.loads(pathlib.Path(args[1]).read_text())
                self.assertEqual(payload, expected_context.model_dump(
                    mode="json", by_alias=True))
                self.assertTrue(pathlib.Path(args[1]).is_absolute())
                call_list.append(args)

            return runner

        for context in contexts:
            calls: list[list[str]] = []
            run_hooks(paths, context, cwd, run=make_runner(context, calls))
            self.assertEqual([call[0]
                             for call in calls], list(map(str, paths)))
            self.assertEqual(len({call[1] for call in calls}), 1)
            self.assertFalse(pathlib.Path(calls[0][1]).exists())
            payload = context.model_dump(mode="json", by_alias=True)
            if isinstance(context, ExecuteHookContext):
                self.assertEqual(
                    set(payload), {"event", "flow", "dst", "reboot", "device_log"})
                self.assertEqual(payload["flow"]["$desc"], "description")
            else:
                self.assertEqual(
                    set(payload), {"event", "dirs", "dst", "smaps_filter_pattern", "smaps_filter_flags", "plots"})
                self.assertEqual(payload["dirs"], [
                                 str(cwd / "b"), str(cwd / "a")])

    def test_failures_are_informative_and_cleanup_without_failure_logging(self) -> None:
        cwd = pathlib.Path.cwd()
        context = ExecuteHookContext(event="pre_execute_flow", flow=Flow(flow=[]),
                                     dst=cwd / "evidence", reboot=False, device_log=False)

        def make_failing_runner(expected_error: Exception,
                                call_list: list[list[str]]) -> typing.Callable[..., None]:
            def failing(args: list[str], *, cwd: pathlib.Path,
                        check: bool, timeout: int) -> None:
                call_list.append(args)
                raise expected_error

            return failing

        for error, reason in (
            (subprocess.CalledProcessError(7, ["hook"]), "exit code 7"),
            (subprocess.TimeoutExpired(["hook"], 300), "300 seconds"),
            (FileNotFoundError("missing executable"), "unable to launch"),
            (ValueError("custom runner failed"), "custom runner failed"),
        ):
            calls: list[list[str]] = []
            with tempfile.TemporaryDirectory() as tmp:
                log = pathlib.Path(tmp) / "log"
                configure_logger("info", log)
                try:
                    with self.assertRaisesRegex(RuntimeError, reason) as caught:
                        run_hooks([cwd / "hook", cwd / "later"],
                                  context, cwd, run=make_failing_runner(error, calls))
                    for value in (context.event, str(cwd / "hook")):
                        self.assertIn(value, str(caught.exception))
                    self.assertNotIn("dst=", str(caught.exception))
                finally:
                    reset_logger()
                lines = log.read_text().splitlines()
                self.assertEqual(len(lines), 1)
                self.assertIn("hook start:", lines[0])
                self.assertNotIn("dst=", lines[0])
            self.assertEqual(len(calls), 1)
            self.assertFalse(pathlib.Path(calls[0][1]).exists())

    def test_cleanup_failure_preserves_original_error(self) -> None:
        cwd = pathlib.Path.cwd()
        context = ExecuteHookContext(event="post_execute_flow", flow=Flow(flow=[]),
                                     dst=cwd / "evidence", reboot=False, device_log=False)
        paths = []

        def failing(args: list[str], *, cwd: pathlib.Path,
                    check: bool, timeout: int) -> None:
            path = pathlib.Path(args[1])
            paths.append(path)
            path.unlink()
            path.mkdir()
            raise subprocess.CalledProcessError(9, args)

        try:
            with self.assertRaisesRegex(RuntimeError, "exit code 9.*failed to remove hook context"):
                run_hooks([cwd / "hook"], context, cwd, run=failing)
        finally:
            for path in paths:
                path.rmdir()

    def test_empty_phase_creates_no_context(self) -> None:
        # Serialization would fail if an empty phase attempted to create a file.
        context = ExecuteHookContext.model_construct(
            event="pre_execute_flow", dst=pathlib.Path("/evidence"),
            reboot=False, device_log=False, flow=typing.cast(Flow, object()))
        run_hooks([], context, pathlib.Path.cwd())

    def test_actual_executable_gets_context_and_inherited_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            hook = root / "hook with spaces"
            hook.write_text(f"#!{sys.executable}\n" +
                            "import json, pathlib, sys\n"
                            "ctx = json.loads(pathlib.Path(sys.argv[1]).read_text())\n"
                            "print(ctx['event'], flush=True)\n"
                            "print(pathlib.Path.cwd(), file=sys.stderr, flush=True)\n")
            hook.chmod(0o755)
            code = (
                "from pathlib import Path; from src.hooks import ExecuteHookContext, run_hooks; "
                "from src.schema import Flow; "
                f"run_hooks([Path({str(hook)!r})], ExecuteHookContext("
                "event='pre_execute_flow', flow=Flow(flow=[]), "
                f"dst=Path({str(root / 'evidence')!r}), reboot=False, device_log=False), "
                f"Path({str(root)!r}))")
            result = subprocess.run([sys.executable, "-c", code], check=True,
                                    capture_output=True, text=True)
            self.assertEqual(result.stdout.strip(), "pre_execute_flow")
            self.assertEqual(result.stderr.strip(), str(root))

    def test_success_logging_and_context_edits_do_not_mutate_model(self) -> None:
        cwd = pathlib.Path.cwd()
        context = ExecuteHookContext(event="pre_execute_flow", flow=Flow(flow=[]),
                                     dst=cwd / "evidence", reboot=False, device_log=False)

        def edit(args: list[str], *, cwd: pathlib.Path, check: bool, timeout: int) -> None:
            pathlib.Path(args[1]).write_text('{"dst": "/changed"}')

        with tempfile.TemporaryDirectory() as tmp:
            levels: tuple[tuple[LogLevel, int], ...] = (
                ("info", 2), ("err", 0))
            for level, count in levels:
                log = pathlib.Path(tmp) / level
                configure_logger(level, log)
                try:
                    run_hooks([cwd / "hook"], context, cwd, run=edit)
                finally:
                    reset_logger()
                self.assertEqual(len(log.read_text().splitlines()), count)
            self.assertEqual(context.dst, cwd / "evidence")


if __name__ == "__main__":
    unittest.main()
