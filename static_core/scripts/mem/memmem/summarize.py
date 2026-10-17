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
import pathlib
import re
import shutil
import sys

from src.path import absolutize
from src.result import ResultStore
from src.report import average_reports

import lib


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Sumarize --out-dir that contains several iteration_N directories")
    parser.add_argument(
        "--out-dir",
        required=True,
        type=pathlib.Path,
        help="Directory to summarize",
    )
    return parser


def completed_iteration_stores(out_dir: pathlib.Path) -> list[ResultStore]:
    iteration_dirs = sorted(
        (path for path in out_dir.iterdir()
         if path.is_dir() and re.fullmatch(r"iteration_\d+", path.name)),
        key=lambda path: int(path.name.removeprefix("iteration_")),
    )
    stores: list[ResultStore] = []
    for path in iteration_dirs:
        store = ResultStore(path, pathlib.PurePosixPath(""))
        if store.local_summary_path().is_file():
            stores.append(store)
        else:
            lib.get_logger().warn(
                f"skipping iteration without summary.csv: {path}")
    if not stores:
        raise RuntimeError(f"no iteration summary.csv found under {out_dir}")
    return stores


def summarize(out_dir: pathlib.Path) -> None:
    summary_dir = out_dir.joinpath("summary")
    backup_dir = out_dir.joinpath(".summary-backup")
    if backup_dir.exists():
        raise RuntimeError(f"summary backup already exists: {backup_dir}")
    if summary_dir.exists():
        summary_dir.rename(backup_dir)
    try:
        average_reports(out_dir, completed_iteration_stores(out_dir))
    except Exception:
        shutil.rmtree(summary_dir, ignore_errors=True)
        if backup_dir.exists():
            backup_dir.rename(summary_dir)
        raise
    if backup_dir.exists():
        shutil.rmtree(backup_dir)


def main() -> int:
    args = build_parser().parse_args()
    out_dir = absolutize(args.out_dir)
    try:
        summarize(out_dir)
    except Exception as error:
        print(f"error: memmem error: {error}", file=sys.stderr)
        return 1
    finally:
        lib.reset_logger()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
