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
import subprocess
import tempfile
from collections.abc import Sequence
from typing import Literal, Protocol

from src.errors import CombinedError
from src.log import get_logger
from src.pydantic_helpers import PydanticModel
from src.schema import Flow


class ExecuteHookContext(PydanticModel):
    event: Literal["pre_execute_flow", "post_execute_flow"]
    flow: Flow
    dst: pathlib.Path
    reboot: bool
    device_log: bool


class ViewHookContext(PydanticModel):
    event: Literal["pre_create_view", "post_create_view"]
    dirs: list[pathlib.Path]
    dst: pathlib.Path
    smaps_filter_pattern: str
    smaps_filter_flags: int
    plots: bool


HookContext = ExecuteHookContext | ViewHookContext
HOOK_TIMEOUT_SECONDS = 300


class ProcessRunner(Protocol):
    def __call__(self, args: list[str], *, cwd: pathlib.Path,
                 check: bool, timeout: int) -> object:
        ...


def _invoke_hook(path: pathlib.Path, context_path: pathlib.Path,
                 context: HookContext, cwd: pathlib.Path, index: int,
                 run: ProcessRunner) -> None:
    tag = f"event={context.event} index={index} path={path}"
    get_logger().info(f"hook start: {tag}")
    try:
        run([str(path), str(context_path)], cwd=cwd,
            check=True, timeout=HOOK_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(
            f"hook failed: {tag}: timed out after {HOOK_TIMEOUT_SECONDS} seconds") from error
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            f"hook failed: {tag}: exit code {error.returncode}") from error
    except OSError as error:
        raise RuntimeError(
            f"hook failed: {tag}: unable to launch: {error}") from error
    except Exception as error:
        raise RuntimeError(f"hook failed: {tag}: {error}") from error
    get_logger().info(f"hook end: {tag}")


def run_hooks(paths: Sequence[pathlib.Path], context: HookContext,
              cwd: pathlib.Path, *, run: ProcessRunner = subprocess.run) -> None:
    if not paths:
        return
    context_path: pathlib.Path | None = None
    errors = CombinedError()
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8",
                                         suffix=".json", delete=False) as stream:
            context_path = pathlib.Path(stream.name).absolute()
            stream.write(context.model_dump_json(
                by_alias=True, indent=2) + "\n")
        for index, path in enumerate(paths, start=1):
            _invoke_hook(path, context_path, context, cwd, index, run)
    except Exception as error:
        errors.add(error, None)
    finally:
        if context_path is not None:
            try:
                context_path.unlink(missing_ok=True)
            except OSError as error:
                errors.add(error, "failed to remove hook context")
    errors.raise_if_any()
