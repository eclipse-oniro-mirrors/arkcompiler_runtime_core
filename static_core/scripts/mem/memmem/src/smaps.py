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

import dataclasses
import re

from src.profile import MemProfile, add_profiles, empty_profile


@dataclasses.dataclass(frozen=True)
class SmapsSummary:
    total: MemProfile
    breakdown: dict[str, MemProfile]


_HEADER_RE = re.compile(
    r"^[0-9A-Fa-f]+-[0-9A-Fa-f]+\s+\S+\s+\S+\s+\S+\s+\S+(?:\s+(?P<tag>.*))?$"
)
_METRIC_RE = re.compile(r"^(?P<name>[A-Za-z0-9_\s]+):\s+(?P<value>\d+)\s+kB$")
MATCH_ALL: re.Pattern[str] = re.compile(".*")


def parse_smaps_text(
    text: str,
    tag_filter: re.Pattern[str] = MATCH_ALL,
) -> SmapsSummary | None:
    breakdown: dict[str, MemProfile] = {}
    total: MemProfile | None = None
    current_tag: str | None = None
    current_profile: MemProfile | None = None

    def flush() -> None:
        nonlocal total
        if current_profile is None or current_tag is None:
            return
        if tag_filter.match(current_tag) is None:
            return
        breakdown[current_tag] = add_profiles(
            breakdown.get(current_tag, empty_profile()),
            current_profile,
        )
        total = current_profile if total is None else add_profiles(
            total, current_profile)

    for raw_line in text.splitlines():
        line = raw_line.rstrip("\n")
        header_match = _HEADER_RE.match(line)
        if header_match is not None:
            flush()
            current_tag = _normalize_tag(header_match.group("tag"))
            current_profile = empty_profile()
            continue

        if current_profile is None:
            continue

        res = _parse_metric_line(line)
        if res is None:
            continue
        name, value = res
        current_profile = _add_metric(current_profile, name, value)

    flush()

    if total is None:
        return None
    return SmapsSummary(total=total, breakdown=breakdown)


def _normalize_tag(tag: str | None) -> str:
    if tag is None or tag.strip() == "":
        return "[anonymous]"
    return tag.strip()


def _parse_metric_line(line: str) -> tuple[str, int] | None:
    match = _METRIC_RE.match(line)
    if match is None:
        return None
    return match.group("name"), int(match.group("value"))


def _add_metric(profile: MemProfile, name: str, value: int) -> MemProfile:
    if name == "Size":
        return dataclasses.replace(profile, size_kb=profile.size_kb + value)
    if name == "Rss":
        return dataclasses.replace(profile, rss_kb=profile.rss_kb + value)
    if name == "Pss":
        return dataclasses.replace(profile, pss_kb=profile.pss_kb + value)
    if name == "Referenced":
        return dataclasses.replace(
            profile, referenced_kb=profile.referenced_kb + value)
    if name == "Shared_Clean":
        return dataclasses.replace(
            profile, shared_clean_kb=profile.shared_clean_kb + value,
            shared_total_kb=profile.shared_total_kb + value)
    if name == "Shared_Dirty":
        return dataclasses.replace(
            profile, shared_dirty_kb=profile.shared_dirty_kb + value,
            shared_total_kb=profile.shared_total_kb + value)
    if name == "Private_Clean":
        return dataclasses.replace(
            profile, private_clean_kb=profile.private_clean_kb + value,
            private_total_kb=profile.private_total_kb + value)
    if name == "Private_Dirty":
        return dataclasses.replace(
            profile, private_dirty_kb=profile.private_dirty_kb + value,
            private_total_kb=profile.private_total_kb + value)
    if name == "Swap":
        return dataclasses.replace(profile, swap_kb=profile.swap_kb + value)
    if name == "SwapPss":
        return dataclasses.replace(
            profile, swap_pss_kb=profile.swap_pss_kb + value)
    if name == "Anonymous":
        return dataclasses.replace(
            profile, anonymous_kb=profile.anonymous_kb + value)
    return profile
