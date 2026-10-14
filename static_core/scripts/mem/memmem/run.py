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
from datetime import datetime
import pathlib
import sys

from src.cli_helpers import (add_execution_arguments, add_logging_arguments,
                             add_view_arguments, execute_iterations,
                             load_device, load_flow, view_destination)
from src.path import absolutize

import lib


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Benchmark mobile device memory usage", allow_abbrev=False)
    add_execution_arguments(parser)
    add_view_arguments(parser)
    add_logging_arguments(parser)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    out_dir = (absolutize(args.out_dir) if args.out_dir is not None else
               absolutize(pathlib.Path(
                   f"memmem-out-{timestamp}")))
    log_file = (absolutize(args.memmem_log_file)
                if args.memmem_log_file is not None else None)
    try:
        lib.configure_logger(args.memmem_log_level, log_file)
        flow = load_flow(args.flow)
        device = load_device(pathlib.Path(".env"), args.backend)
        evidence_dirs = execute_iterations(
            flow, device, out_dir, args.repeats, reboot=args.reboot,
            device_log=args.device_log)
        for index, evidence_dir in enumerate(evidence_dirs):
            lib.create_view([evidence_dir], view_destination(
                out_dir / f"iteration_{index}", timestamp), args.smaps_filter)
        lib.create_view(evidence_dirs, view_destination(out_dir, timestamp),
                        args.smaps_filter)
    except Exception as error:
        print(f"memmem error: {error}", file=sys.stderr)
        return 1
    finally:
        lib.reset_logger()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
