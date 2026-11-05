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
import csv
import dataclasses
from decimal import Decimal, InvalidOperation
import pathlib
import sys

from src.cli_helpers import add_logging_arguments
from src.path import absolutize
from src.store import ViewStore
from src.smaps import SUMMARY_METRICS
from src.view import ParsedView, is_view_equivalent, read_view

import lib


@dataclasses.dataclass(frozen=True)
class Value:
    text: str
    number: Decimal


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare memory benchmark views", allow_abbrev=False)
    parser.add_argument(
        "--base", required=True, type=pathlib.Path,
        help="path to the view used as comparison base")
    parser.add_argument(
        "--change", action="append", required=True, type=pathlib.Path,
        help="path to a view compared to --base; may be repeated")
    parser.add_argument(
        "--out-dir", required=True, type=pathlib.Path,
        help="path to the directory where comparison results are emitted")
    add_logging_arguments(parser)
    return parser


def _read_value(row: dict[str, str], column: str, path: pathlib.Path) -> Value:
    try:
        text = row[column]
    except KeyError as error:
        raise ValueError(f"missing column {column} in {path}") from error
    try:
        number = Decimal(text)
    except InvalidOperation as error:
        raise ValueError(
            f"invalid value for {column} in {path}: {text}") from error
    if not number.is_finite():
        raise ValueError(f"invalid value for {column} in {path}: {text}")
    return Value(text=text, number=number)


def _percent(base: Value, change: Value) -> str:
    if base.number == 0:
        return ""
    return f"{((change.number - base.number) / base.number * 100):.2f}"


def _comparison_header(identity: list[str], count: int) -> list[str]:
    header = identity.copy()
    for metric in SUMMARY_METRICS:
        header.append(f"{metric}_base")
        for index in range(count):
            header.extend([
                f"{metric}_change_{index}",
                f"{metric}_change_{index}_pct",
            ])
    return header


def _comparison_row(
    identity: list[str],
    base_row: dict[str, str] | None,
    change_rows: list[dict[str, str] | None],
    base_path: pathlib.Path,
    change_paths: list[pathlib.Path],
) -> list[str]:
    row = identity.copy()
    for metric in SUMMARY_METRICS:
        column = f"{metric}_median"
        base = (None if base_row is None else _read_value(
            base_row, column, base_path))
        row.append("" if base is None else base.text)
        for index, change_row in enumerate(change_rows):
            if change_row is None:
                row.extend(["", ""])
                continue
            change = _read_value(change_row, column, change_paths[index])
            row.extend(
                [change.text, "" if base is None else _percent(base, change)])
    return row


def _breakdown_tags(
    base_rows: list[dict[str, str]],
    change_rows: list[list[dict[str, str]]],
) -> list[str]:
    tags: list[str] = []
    known: set[str] = set()
    for rows in [base_rows, *change_rows]:
        for row in rows:
            tag = row["tag"]
            if tag not in known:
                tags.append(tag)
                known.add(tag)
    return tags


def _rows_by_tag(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {row["tag"]: row for row in rows}


def _write_csv(path: pathlib.Path, rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream).writerows(rows)


def _comparison_rows(base: ParsedView, changes: list[ParsedView]) -> tuple[
        list[list[str]], dict[tuple[str, str], list[list[str]]]]:
    change_summary_paths = [view.store.local_summary_path()
                            for view in changes]
    summary_rows = [_comparison_header(
        ["app_label", "snapshot_label"], len(changes))]
    for index, base_row in enumerate(base.summary_rows):
        summary_rows.append(_comparison_row(
            [base_row["app_label"], base_row["snapshot_label"]], base_row,
            [view.summary_rows[index] for view in changes],
            base.store.local_summary_path(), change_summary_paths))

    breakdowns: dict[tuple[str, str], list[list[str]]] = {}
    for identity, base_rows in base.breakdown_rows.items():
        change_rows = [view.breakdown_rows[identity] for view in changes]
        change_paths = [view.store.local_breakdown_path(identity[1], identity[0])
                        for view in changes]
        rows = [_comparison_header(["tag"], len(changes))]
        base_by_tag = _rows_by_tag(base_rows)
        changes_by_tag = [_rows_by_tag(values) for values in change_rows]
        for tag in _breakdown_tags(base_rows, change_rows):
            rows.append(_comparison_row(
                [tag], base_by_tag.get(tag),
                [values.get(tag) for values in changes_by_tag],
                base.store.local_breakdown_path(identity[1], identity[0]),
                change_paths))
        breakdowns[identity] = rows
    return summary_rows, breakdowns


def compare_reports(
    base_path: pathlib.Path,
    change_paths: list[pathlib.Path],
    out_dir: pathlib.Path,
) -> None:
    """Compare standalone views; matching declarations are caller-owned semantics."""
    base = read_view(ViewStore(base_path))
    changes = [read_view(ViewStore(path)) for path in change_paths]
    for index, change in enumerate(changes):
        if not is_view_equivalent(base, change):
            raise ValueError(
                f"change {index} view is incompatible with base view")

    # Build every row before reserving the destination, so invalid numeric input
    # never leaves an output directory behind.
    summary_rows, breakdowns = _comparison_rows(base, changes)
    out_dir.mkdir(parents=True, exist_ok=False)
    _write_csv(out_dir.joinpath("summary.csv"), summary_rows)
    for (app, snapshot), rows in breakdowns.items():
        _write_csv(out_dir.joinpath(
            "breakdowns", snapshot, f"{app}.csv"), rows)


def main() -> int:
    args = build_parser().parse_args()
    log_file = (absolutize(args.memmem_log_file)
                if args.memmem_log_file is not None else None)
    try:
        lib.configure_logger(args.memmem_log_level, log_file)
        compare_reports(
            absolutize(args.base),
            [absolutize(path) for path in args.change],
            absolutize(args.out_dir),
        )
    except Exception as error:
        print(f"memmem error: {error}", file=sys.stderr)
        return 1
    finally:
        lib.reset_logger()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
