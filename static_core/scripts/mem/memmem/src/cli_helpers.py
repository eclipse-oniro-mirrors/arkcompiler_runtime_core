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

import argparse
import dataclasses
import json
import pathlib
import re

from dotenv import dotenv_values

from src.device import Device
from src.hooks import ExecuteHookContext, ViewHookContext, run_hooks
from src.path import absolutize
from src.schema import Flow, UnprocessedFlow
from src.types import positive_int

import lib


@dataclasses.dataclass(frozen=True)
class Config:
    hdc_path: pathlib.Path | None = None
    abd_path: pathlib.Path | None = None


def add_logging_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--memmem-log-level", choices=["info", "warn", "err"],
                        default="err")
    parser.add_argument("--memmem-log-file", type=pathlib.Path, default=None)


def add_execution_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--pre-execute-flow-hook", action="append",
                        type=pathlib.Path, default=[], metavar="PATH",
                        help="executable hook; repeat to run in order")
    parser.add_argument("--post-execute-flow-hook", action="append",
                        type=pathlib.Path, default=[], metavar="PATH",
                        help="executable hook; repeat to run in order")
    parser.add_argument("--flow", required=True, type=pathlib.Path)
    parser.add_argument("--backend", choices=["hdc", "abd"], default="hdc")
    parser.add_argument("--out-dir", type=pathlib.Path, default=None,
                        help="parent directory for benchmark output")
    parser.add_argument("--repeats", type=positive_int, default=1,
                        help="number of times to repeat the flow; positive int")
    reboot_group = parser.add_mutually_exclusive_group()
    reboot_group.add_argument("--reboot", dest="reboot", action="store_true",
                              default=False)
    reboot_group.add_argument(
        "--no-reboot", dest="reboot", action="store_false")
    device_log_group = parser.add_mutually_exclusive_group()
    device_log_group.add_argument("--device-log", dest="device_log",
                                  action="store_true", default=True)
    device_log_group.add_argument("--no-device-log", dest="device_log",
                                  action="store_false")


def add_view_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--pre-create-view-hook", action="append",
                        type=pathlib.Path, default=[], metavar="PATH",
                        help="executable hook; repeat to run in order")
    parser.add_argument("--post-create-view-hook", action="append",
                        type=pathlib.Path, default=[], metavar="PATH",
                        help="executable hook; repeat to run in order")
    parser.add_argument(
        "--smaps-filter",
        type=compile_smaps_filter,
        default=re.compile(".*"),
        help=("regex matched with re.match against each normalized smaps tag; "
              "only matching mappings are counted in views; e.g. '.*\\.so'"),
    )
    plots_group = parser.add_mutually_exclusive_group()
    plots_group.add_argument("--plots", dest="plots", action="store_true",
                             default=True)
    plots_group.add_argument("--no-plots", dest="plots", action="store_false")


def compile_smaps_filter(value: str) -> re.Pattern[str]:
    try:
        return re.compile(value)
    except re.error:
        raise argparse.ArgumentTypeError(f"invalid regex: {value}") from None


def load_flow(path: pathlib.Path) -> Flow:
    flow_path = absolutize(path)
    unprocessed = UnprocessedFlow.model_validate(
        json.loads(flow_path.read_text(encoding="utf-8")))
    return lib.preprocess_flow(unprocessed)


def load_config(env_path: pathlib.Path) -> Config:
    resolved_env_path = absolutize(env_path)
    if not resolved_env_path.is_file():
        raise FileNotFoundError(
            f"env file does not exist: {resolved_env_path}")
    values = dotenv_values(resolved_env_path)

    def optional_path(name: str) -> pathlib.Path | None:
        value = values.get(name)
        return None if value is None or value == "" else absolutize(pathlib.Path(value))

    return Config(hdc_path=optional_path("HDC_PATH"),
                  abd_path=optional_path("ABD_PATH"))


def load_device(env_path: pathlib.Path, backend: str) -> Device:
    config = load_config(env_path)
    if backend == "hdc":
        if config.hdc_path is None:
            raise ValueError("HDC_PATH is required when backend=hdc")
        return lib.get_hdc_device(lib.get_hdc(config.hdc_path))
    if backend == "abd":
        if config.abd_path is None:
            raise ValueError("ABD_PATH is required when backend=abd")
        return lib.get_abd_device(lib.get_abd(config.abd_path))
    raise ValueError(f"unsupported backend: {backend}")


def execute_iterations(
    flow: Flow,
    device: Device,
    out_dir: pathlib.Path,
    repeats: int,
    *,
    reboot: bool,
    device_log: bool,
    pre_hooks: list[pathlib.Path],
    post_hooks: list[pathlib.Path],
) -> list[pathlib.Path]:
    pre_hooks = [absolutize(path) for path in pre_hooks]
    post_hooks = [absolutize(path) for path in post_hooks]
    if repeats <= 0:
        raise ValueError("repeats must be positive")
    root = absolutize(out_dir)
    destinations = [root / f"iteration_{index}" / "evidence"
                    for index in range(repeats)]
    root.mkdir(parents=True, exist_ok=True)
    for destination in destinations:
        if destination.exists():
            raise FileExistsError(
                f"evidence destination already exists: {destination}")
    results = []
    for destination in destinations:
        results.append(_execute_flow_with_hooks(
            flow, device, destination, reboot=reboot, device_log=device_log,
            pre_hooks=pre_hooks, post_hooks=post_hooks))
    return results


def _execute_flow_with_hooks(
    flow: Flow, device: Device, dst: pathlib.Path, *, reboot: bool,
    device_log: bool, pre_hooks: list[pathlib.Path], post_hooks: list[pathlib.Path],
) -> pathlib.Path:
    cwd = pathlib.Path.cwd()

    context_before = ExecuteHookContext(event="pre_execute_flow", flow=flow,
                                        dst=dst, reboot=reboot, device_log=device_log)
    run_hooks(pre_hooks, context_before, cwd)

    result = lib.execute_flow(flow, device, dst,
                              reboot=reboot, device_log=device_log)

    context_after = ExecuteHookContext(event="post_execute_flow", flow=flow,
                                       dst=dst, reboot=reboot, device_log=device_log)
    run_hooks(post_hooks, context_after, cwd)

    return result


def create_view_with_hooks(
    dirs: list[pathlib.Path],
    dst: pathlib.Path,
    *,
    smaps_filter: re.Pattern[str],
    plots: bool,
    pre_hooks: list[pathlib.Path],
    post_hooks: list[pathlib.Path],
) -> pathlib.Path:
    cwd = pathlib.Path.cwd()
    sources = [absolutize(path) for path in dirs]
    destination = absolutize(dst)
    before = [absolutize(path) for path in pre_hooks]
    after = [absolutize(path) for path in post_hooks]

    context_before = ViewHookContext(event="pre_create_view", dirs=sources, dst=destination,
                                     smaps_filter_pattern=smaps_filter.pattern,
                                     smaps_filter_flags=smaps_filter.flags, plots=plots)
    run_hooks(before, context_before, cwd)

    result = lib.create_view(sources, destination,
                             smaps_filter=smaps_filter, plots=plots)

    context_after = ViewHookContext(event="post_create_view", dirs=sources, dst=destination,
                                    smaps_filter_pattern=smaps_filter.pattern,
                                    smaps_filter_flags=smaps_filter.flags, plots=plots)
    run_hooks(after, context_after, cwd)

    return result


def view_destination(out_dir: pathlib.Path, timestamp: str) -> pathlib.Path:
    return absolutize(out_dir) / "view" / f"view-{timestamp}"
