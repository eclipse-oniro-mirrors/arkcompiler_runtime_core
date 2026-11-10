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

from src.cli_helpers import (add_logging_arguments, add_view_arguments,
                             view_destination, create_view_with_hooks)
from src.path import absolutize

import lib


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create a memory view from evidence", allow_abbrev=False)
    parser.add_argument("dirs", nargs="+", type=pathlib.Path,
                        help="evidence directories in declared order")
    parser.add_argument("--out-dir", required=True, type=pathlib.Path,
                        help="parent directory for the new view")
    add_view_arguments(parser)
    add_logging_arguments(parser)
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    log_file = (absolutize(args.memmem_log_file)
                if args.memmem_log_file is not None else None)
    try:
        lib.configure_logger(args.memmem_log_level, log_file)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        create_view_with_hooks(args.dirs, view_destination(args.out_dir, timestamp),
                               smaps_filter=args.smaps_filter,
                               plots=args.plots, pre_hooks=args.pre_create_view_hook,
                               post_hooks=args.post_create_view_hook)
    except Exception as error:
        print(f"memmem error: {error}", file=sys.stderr)
        return 1
    finally:
        lib.reset_logger()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
