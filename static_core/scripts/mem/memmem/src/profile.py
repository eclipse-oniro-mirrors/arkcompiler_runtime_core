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


@dataclasses.dataclass(frozen=True)
class MemProfile:
    size_kb: int
    rss_kb: int
    pss_kb: int
    referenced_kb: int
    shared_clean_kb: int
    shared_dirty_kb: int
    shared_total_kb: int
    private_clean_kb: int
    private_dirty_kb: int
    private_total_kb: int
    swap_kb: int
    swap_pss_kb: int
    anonymous_kb: int


@dataclasses.dataclass(frozen=True)
class Footprint:
    footprint: int
    pss_plus_swap_pss: int
    private_clean_kb: int


SUMMARY_METRICS = [field.name for field in dataclasses.fields(MemProfile)]
FOOTPRINT_METRICS = [field.name for field in dataclasses.fields(Footprint)]


def footprint_from_profile(profile: MemProfile) -> Footprint:
    return Footprint(
        footprint=profile.pss_kb + 2 * profile.swap_pss_kb,
        pss_plus_swap_pss=profile.pss_kb + profile.swap_pss_kb,
        private_clean_kb=profile.private_clean_kb,
    )


def empty_profile() -> MemProfile:
    return MemProfile(*([0] * len(SUMMARY_METRICS)))


def add_profiles(left: MemProfile, right: MemProfile) -> MemProfile:
    return MemProfile(*(getattr(left, metric) + getattr(right, metric)
                        for metric in SUMMARY_METRICS))
