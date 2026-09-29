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
import shlex
import subprocess
import typing

from src.device import CommandResult
from src.log import get_logger
from src.connection import TIMEOUT_ERR_CODE


class Abd:
    def __init__(self, abd_path: pathlib.Path) -> None:
        self.abd_path = abd_path

    def run(self, *args: str, timeout: int = -1) -> CommandResult:
        cmd = [str(self.abd_path), *args]
        get_logger().info(f"$ {' '.join(cmd)}")
        try:
            completed = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                check=False,
                timeout=None if timeout == -1 else timeout,
            )
        except subprocess.TimeoutExpired:
            return CommandResult(TIMEOUT_ERR_CODE, "", f"memmem: TIMEOUT {cmd}")
        return CommandResult(completed.returncode, completed.stdout, completed.stderr)

    def shell(self, *args: str, timeout: int = -1) -> CommandResult:
        if not args:
            return self.run("shell", timeout=timeout)
        return self.run("shell", shlex.join(args), timeout=timeout)

    def shell_raw(self, command: str, timeout: int = -1) -> CommandResult:
        return self.run("shell", command, timeout=timeout)

    def start_shell(
        self,
        command: str,
        stdout: int | None = subprocess.DEVNULL,
        stderr: int | None = subprocess.DEVNULL,
    ) -> subprocess.Popen[typing.Any]:
        return subprocess.Popen(
            [str(self.abd_path), "shell", command],
            stdout=stdout,
            stderr=stderr,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
