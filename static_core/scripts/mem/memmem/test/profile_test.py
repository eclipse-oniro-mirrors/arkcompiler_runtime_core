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

import unittest

from src.profile import (FOOTPRINT_METRICS, Footprint, MemProfile,
                         SUMMARY_METRICS, footprint_from_profile)


class ProfileTest(unittest.TestCase):
    def test_footprint_from_profile_derives_ordered_metrics(self) -> None:
        profile = MemProfile(*range(1, len(SUMMARY_METRICS) + 1))

        self.assertEqual(footprint_from_profile(profile), Footprint(
            footprint=profile.pss_kb + 2 * profile.swap_pss_kb,
            pss_plus_swap_pss=profile.pss_kb + profile.swap_pss_kb,
            private_clean_kb=profile.private_clean_kb,
        ))
        self.assertEqual(FOOTPRINT_METRICS, [
            "footprint", "pss_plus_swap_pss", "private_clean_kb"])


if __name__ == "__main__":
    unittest.main()
