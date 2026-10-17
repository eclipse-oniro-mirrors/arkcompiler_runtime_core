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
import shutil
import sys
import tempfile

from src.path import absolutize
from src.smaps import SUMMARY_METRICS


@dataclasses.dataclass(frozen=True)
class Value:
    text: str
    number: Decimal


@dataclasses.dataclass(frozen=True)
class Report:
    root: pathlib.Path
    use_median: bool
    summary_rows: list[dict[str, str]]
    summary_path: pathlib.Path
    breakdowns: dict[pathlib.PurePath, dict[str, dict[str, str]]]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare memory benchmark result directories")
    parser.add_argument(
        "--base",
        required=True,
        type=pathlib.Path,
        help="path to the directory with run.py output used as comparison base",
    )
    parser.add_argument(
        "--change",
        action="append",
        required=True,
        type=pathlib.Path,
        help="path to run.py output compared to --base; may be repeated",
    )
    parser.add_argument(
        "--out-dir",
        required=True,
        type=pathlib.Path,
        help="path to the directory where comparison results are emitted",
    )
    return parser


def _report_root(path: pathlib.Path) -> pathlib.Path:
    if not path.is_dir():
        raise ValueError(f"run output directory does not exist: {path}")
    summary_root = path.joinpath("summary")
    return summary_root if summary_root.is_dir() else path


def _read_csv(path: pathlib.Path) -> tuple[list[str], list[dict[str, str]]]:
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames is None:
                raise ValueError("missing header")
            if len(set(reader.fieldnames)) != len(reader.fieldnames):
                raise ValueError("duplicate header")
            return list(reader.fieldnames), list(reader)
    except OSError as error:
        raise ValueError(f"cannot read {path}: {error}") from error


def _required_columns(
    header: list[str],
    prefix: str,
    use_median: bool,
) -> list[str]:
    suffix = "_median" if use_median else ""
    columns = [f"{metric}{suffix}" for metric in SUMMARY_METRICS]
    missing = [column for column in [prefix, *columns] if column not in header]
    if missing:
        raise ValueError(f"missing columns in report: {', '.join(missing)}")
    return columns


def _breakdown_paths(root: pathlib.Path) -> list[pathlib.PurePath]:
    breakdowns = root.joinpath("breakdowns")
    if not breakdowns.exists():
        return []
    if not breakdowns.is_dir():
        raise ValueError(f"breakdowns is not a directory: {breakdowns}")
    paths: list[pathlib.PurePath] = []
    for path in breakdowns.rglob("*"):
        if path.is_file():
            if path.suffix != ".csv":
                raise ValueError(f"unexpected breakdown file: {path}")
            paths.append(path.relative_to(breakdowns))
    return sorted(paths)


def _load_report(path: pathlib.Path) -> Report:
    root = _report_root(path)
    summary_path = root.joinpath("summary.csv")
    if not summary_path.is_file():
        raise ValueError(f"missing summary.csv: {summary_path}")
    filtered_path = root.joinpath("summary_filtered.csv")
    use_median = filtered_path.is_file()
    selected_path = filtered_path if use_median else summary_path
    header, rows = _read_csv(selected_path)
    columns = _required_columns(header, "app_label", use_median)
    _required_columns(header, "snapshot_label", use_median)
    _validate_rows(rows, columns, selected_path)
    breakdowns = {
        relative_path: _read_breakdown(root, relative_path, use_median)
        for relative_path in _breakdown_paths(root)
    }
    return Report(
        root=root,
        use_median=use_median,
        summary_rows=rows,
        summary_path=selected_path,
        breakdowns=breakdowns,
    )


def _row_keys(rows: list[dict[str, str]], key: str) -> list[str]:
    try:
        return [row[key] for row in rows]
    except KeyError as error:
        raise ValueError(f"missing {key} column") from error


def _verify_layout(base: Report, changes: list[Report]) -> None:
    base_apps = _row_keys(base.summary_rows, "app_label")
    base_snapshots = _row_keys(base.summary_rows, "snapshot_label")
    for index, change in enumerate(changes):
        if len(change.summary_rows) != len(base.summary_rows):
            raise ValueError(
                f"change {index} has a different number of summary rows")
        if _row_keys(change.summary_rows, "app_label") != base_apps:
            raise ValueError(f"change {index} has different app_label values")
        if _row_keys(change.summary_rows, "snapshot_label") != base_snapshots:
            raise ValueError(
                f"change {index} has different snapshot_label values")
        if change.breakdowns.keys() != base.breakdowns.keys():
            raise ValueError(
                f"change {index} has a different breakdowns layout")


def _metric_columns(use_median: bool) -> list[str]:
    suffix = "_median" if use_median else ""
    return [f"{metric}{suffix}" for metric in SUMMARY_METRICS]


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


def _validate_rows(
    rows: list[dict[str, str]],
    columns: list[str],
    path: pathlib.Path,
) -> None:
    for row in rows:
        for column in columns:
            _read_value(row, column, path)


def _percent(base: Value, change: Value) -> str:
    if base.number == 0:
        return ""
    return f"{((change.number - base.number) / base.number * 100):.2f}"


def _comparison_header(identity: str, count: int) -> list[str]:
    header = [identity]
    for metric in SUMMARY_METRICS:
        header.append(f"{metric}_base")
        for index in range(count):
            header.extend([
                f"{metric}_change_{index}",
                f"{metric}_change_{index}_pct",
            ])
    return header


def _comparison_row(
    identity: str,
    base_row: dict[str, str] | None,
    change_rows: list[dict[str, str] | None],
    base_columns: list[str],
    change_columns: list[list[str]],
    base_path: pathlib.Path,
    change_paths: list[pathlib.Path],
) -> list[str]:
    row = [identity]
    for metric_index, _metric in enumerate(SUMMARY_METRICS):
        base = (None if base_row is None else _read_value(
            base_row, base_columns[metric_index], base_path))
        row.append("" if base is None else base.text)
        for change_index, change_row in enumerate(change_rows):
            if change_row is None:
                row.extend(["", ""])
                continue
            change = _read_value(
                change_row,
                change_columns[change_index][metric_index],
                change_paths[change_index],
            )
            row.extend(
                [change.text, "" if base is None else _percent(base, change)])
    return row


def _read_breakdown(
    root: pathlib.Path,
    relative_path: pathlib.PurePath,
    use_median: bool,
) -> dict[str, dict[str, str]]:
    path = root.joinpath("breakdowns", relative_path)
    header, rows = _read_csv(path)
    columns = _required_columns(header, "tag", use_median)
    _validate_rows(rows, columns, path)
    by_tag: dict[str, dict[str, str]] = {}
    for row in rows:
        tag = row["tag"]
        if tag in by_tag:
            raise ValueError(f"duplicate tag in {path}: {tag}")
        by_tag[tag] = row
    return by_tag


def _write_csv(path: pathlib.Path, rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream).writerows(rows)


def _breakdown_tags(
    base_rows: dict[str, dict[str, str]],
    change_rows: list[dict[str, dict[str, str]]],
) -> list[str]:
    tags = list(base_rows)
    known = set(tags)
    for rows in change_rows:
        for tag in rows:
            if tag not in known:
                tags.append(tag)
                known.add(tag)
    return tags


def compare_reports(
    base_path: pathlib.Path,
    change_paths: list[pathlib.Path],
    out_dir: pathlib.Path,
) -> None:
    base = _load_report(base_path)
    changes = [_load_report(path) for path in change_paths]
    _verify_layout(base, changes)

    base_columns = _metric_columns(base.use_median)
    change_columns = [_metric_columns(change.use_median) for change in changes]
    change_summary_paths = [change.summary_path for change in changes]
    header = _comparison_header("app_label", len(changes))
    summary_rows = [header]
    for row_index, base_row in enumerate(base.summary_rows):
        row = _comparison_row(
            base_row["app_label"],
            base_row,
            [change.summary_rows[row_index] for change in changes],
            base_columns,
            change_columns,
            base.summary_path,
            change_summary_paths,
        )
        row.insert(1, base_row["snapshot_label"])
        summary_rows.append(row)
    summary_rows[0].insert(1, "snapshot_label")

    breakdown_header = _comparison_header("tag", len(changes))
    breakdowns: dict[pathlib.PurePath, list[list[str]]] = {}
    for relative_path, base_rows in base.breakdowns.items():
        change_rows = [change.breakdowns[relative_path] for change in changes]
        rows = [breakdown_header]
        for tag in _breakdown_tags(base_rows, change_rows):
            rows.append(_comparison_row(
                tag,
                base_rows.get(tag),
                [rows_by_tag.get(tag) for rows_by_tag in change_rows],
                base_columns,
                change_columns,
                base.root.joinpath("breakdowns", relative_path),
                [change.root.joinpath("breakdowns", relative_path)
                 for change in changes],
            ))
        breakdowns[relative_path] = rows

    out_dir.mkdir(parents=True, exist_ok=False)
    try:
        _write_csv(out_dir.joinpath("summary.csv"), summary_rows)
        for relative_path, rows in breakdowns.items():
            _write_csv(out_dir.joinpath("breakdowns", relative_path), rows)
    except Exception:
        shutil.rmtree(out_dir, ignore_errors=True)
        raise


def main() -> int:
    args = build_parser().parse_args()
    try:
        compare_reports(
            absolutize(args.base),
            [absolutize(path) for path in args.change],
            absolutize(args.out_dir),
        )
    except Exception as error:
        print(f"error: memmem error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
