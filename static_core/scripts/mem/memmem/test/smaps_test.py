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
import re
import unittest

from src.profile import MemProfile
from src.smaps import parse_smaps_text


_FIXTURES = pathlib.Path(__file__).parent / "fixtures"


def _profile(size: int, rss: int, pss: int, referenced: int, shared: int,
             private: int, swap: int, swap_pss: int,
             anonymous: int, private_clean: int = 0) -> MemProfile:
    return MemProfile(size, rss, pss, referenced, shared, 0, shared,
                      private_clean, private - private_clean, private, swap,
                      swap_pss, anonymous)


class SmapsTest(unittest.TestCase):
    def test_smaps_python(self) -> None:
        summary = parse_smaps_text(
            (_FIXTURES / "smaps_python.smaps").read_text())
        assert summary is not None
        self.assertEqual(summary.total, _profile(
            14896, 10048, 5178, 10048, 5792, 4256, 0, 0, 4256))
        self.assertEqual(summary.breakdown, {
            "/usr/bin/python3.10": _profile(5776, 3528, 1112, 3528, 3248, 280, 0, 0, 280),
            "/usr/lib/locale/C.utf8/LC_CTYPE": _profile(348, 128, 14, 128, 128, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/gconv/gconv-modules.cache": _profile(28, 28, 1, 28, 28, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2": _profile(236, 236, 19, 236, 220, 16, 0, 0, 16),
            "/usr/lib/x86_64-linux-gnu/libc.so.6": _profile(2160, 1560, 55, 1560, 1536, 24, 0, 0, 24),
            "/usr/lib/x86_64-linux-gnu/libexpat.so.1.8.7": _profile(196, 92, 24, 92, 80, 12, 0, 0, 12),
            "/usr/lib/x86_64-linux-gnu/libm.so.6": _profile(924, 484, 28, 484, 476, 8, 0, 0, 8),
            "/usr/lib/x86_64-linux-gnu/libz.so.1.2.11": _profile(112, 80, 17, 80, 72, 8, 0, 0, 8),
            "[anonymous]": _profile(3960, 2904, 2904, 2904, 0, 2904, 0, 0, 2904),
            "[heap]": _profile(992, 928, 928, 928, 0, 928, 0, 0, 928),
            "[stack]": _profile(132, 76, 76, 76, 0, 76, 0, 0, 76),
            "[vdso]": _profile(8, 4, 0, 4, 4, 0, 0, 0, 0),
            "[vvar]": _profile(16, 0, 0, 0, 0, 0, 0, 0, 0),
            "[vvar_vclock]": _profile(8, 0, 0, 0, 0, 0, 0, 0, 0),
        })

    def test_smaps_self(self) -> None:
        summary = parse_smaps_text(
            (_FIXTURES / "smaps_self.smaps").read_text())
        assert summary is not None
        self.assertEqual(summary.total, _profile(
            4312, 2420, 368, 2420, 2108, 312, 0, 0, 156, 156))
        self.assertEqual(summary.breakdown, {
            "/usr/bin/cp": _profile(144, 144, 144, 144, 0, 144, 0, 0, 8, 136),
            "/usr/lib/locale/C.utf8/LC_ADDRESS": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_COLLATE": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_CTYPE": _profile(348, 128, 14, 128, 128, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_IDENTIFICATION": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MEASUREMENT": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MESSAGES/SYS_LC_MESSAGES": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MONETARY": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_NAME": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_NUMERIC": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_PAPER": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_TELEPHONE": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_TIME": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/gconv/gconv-modules.cache": _profile(28, 28, 1, 28, 28, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2": _profile(236, 236, 19, 236, 220, 16, 0, 0, 16),
            "/usr/lib/x86_64-linux-gnu/libacl.so.1.1.2301": _profile(40, 32, 9, 32, 24, 8, 0, 0, 8),
            "/usr/lib/x86_64-linux-gnu/libattr.so.1.1.2501": _profile(32, 28, 28, 28, 0, 28, 0, 0, 8, 20),
            "/usr/lib/x86_64-linux-gnu/libc.so.6": _profile(2160, 1496, 53, 1496, 1472, 24, 0, 0, 24),
            "/usr/lib/x86_64-linux-gnu/libpcre2-8.so.0.10.4": _profile(604, 80, 11, 80, 72, 8, 0, 0, 8),
            "/usr/lib/x86_64-linux-gnu/libselinux.so.1": _profile(168, 124, 13, 124, 116, 8, 0, 0, 8),
            "[anonymous]": _profile(212, 44, 44, 44, 0, 44, 0, 0, 44),
            "[heap]": _profile(132, 20, 20, 20, 0, 20, 0, 0, 20),
            "[stack]": _profile(132, 12, 12, 12, 0, 12, 0, 0, 12),
            "[vdso]": _profile(8, 4, 0, 4, 4, 0, 0, 0, 0),
            "[vvar]": _profile(16, 0, 0, 0, 0, 0, 0, 0, 0),
            "[vvar_vclock]": _profile(8, 0, 0, 0, 0, 0, 0, 0, 0),
        })

    def test_smaps_shell(self) -> None:
        summary = parse_smaps_text(
            (_FIXTURES / "smaps_shell.smaps").read_text())
        assert summary is not None
        self.assertEqual(summary.total, _profile(
            4792, 3352, 483, 3352, 3080, 272, 0, 0, 272))
        self.assertEqual(summary.breakdown, {
            "/usr/bin/bash": _profile(1364, 1116, 204, 1116, 1064, 52, 0, 0, 52),
            "/usr/lib/locale/C.utf8/LC_ADDRESS": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_COLLATE": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_CTYPE": _profile(348, 128, 10, 128, 128, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_IDENTIFICATION": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MEASUREMENT": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MESSAGES/SYS_LC_MESSAGES": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_MONETARY": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_NAME": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_NUMERIC": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_PAPER": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_TELEPHONE": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/locale/C.utf8/LC_TIME": _profile(4, 4, 0, 4, 4, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/gconv/gconv-modules.cache": _profile(28, 28, 1, 28, 28, 0, 0, 0, 0),
            "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2": _profile(236, 236, 19, 236, 220, 16, 0, 0, 16),
            "/usr/lib/x86_64-linux-gnu/libc.so.6": _profile(2160, 1496, 53, 1496, 1472, 24, 0, 0, 24),
            "/usr/lib/x86_64-linux-gnu/libtinfo.so.6.3": _profile(200, 140, 36, 140, 120, 20, 0, 0, 20),
            "[anonymous]": _profile(116, 52, 52, 52, 0, 52, 0, 0, 52),
            "[heap]": _profile(132, 92, 92, 92, 0, 92, 0, 0, 92),
            "[stack]": _profile(132, 16, 16, 16, 0, 16, 0, 0, 16),
            "[vdso]": _profile(8, 4, 0, 4, 4, 0, 0, 0, 0),
            "[vvar]": _profile(16, 0, 0, 0, 0, 0, 0, 0, 0),
            "[vvar_vclock]": _profile(8, 0, 0, 0, 0, 0, 0, 0, 0),
        })

    def test_clean_dirty_and_totals_are_preserved(self) -> None:
        summary = parse_smaps_text(
            "1-2 rw-p 0 00:00 0 [heap]\n"
            "Shared_Clean: 2 kB\nShared_Dirty: 3 kB\n"
            "Private_Clean: 5 kB\nPrivate_Dirty: 7 kB\n")
        assert summary is not None
        profile = summary.total
        self.assertEqual((profile.shared_clean_kb, profile.shared_dirty_kb,
                          profile.shared_total_kb), (2, 3, 5))
        self.assertEqual((profile.private_clean_kb, profile.private_dirty_kb,
                          profile.private_total_kb), (5, 7, 12))

    def test_profiles_add_clean_dirty_and_total_metrics(self) -> None:
        summary = parse_smaps_text(
            "1-2 rw-p 0 00:00 0 first\nShared_Clean: 1 kB\nPrivate_Dirty: 2 kB\n"
            "3-4 rw-p 0 00:00 0 second\nShared_Dirty: 3 kB\nPrivate_Clean: 4 kB\n")
        assert summary is not None
        self.assertEqual(summary.total, MemProfile(
            0, 0, 0, 0, 1, 3, 4, 4, 2, 6, 0, 0, 0))

    def test_empty_input(self) -> None:
        self.assertIsNone(parse_smaps_text(""))

    def test_mapping_without_metrics_is_zero_profile(self) -> None:
        summary = parse_smaps_text("1-2 rw-p 0 00:00 0 /bin/x\n")
        assert summary is not None
        self.assertEqual(summary.breakdown, {
                         "/bin/x": MemProfile(*([0] * 13))})
        self.assertEqual(summary.total, MemProfile(*([0] * 13)))

    def test_tag_filter_keeps_matching_mappings_only(self) -> None:
        summary = parse_smaps_text((_FIXTURES / "smaps_shell.smaps").read_text(),
                                   re.compile(r".*\.so"))
        assert summary is not None
        self.assertEqual(summary.breakdown, {
            "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2": _profile(236, 236, 19, 236, 220, 16, 0, 0, 16),
            "/usr/lib/x86_64-linux-gnu/libc.so.6": _profile(2160, 1496, 53, 1496, 1472, 24, 0, 0, 24),
            "/usr/lib/x86_64-linux-gnu/libtinfo.so.6.3": _profile(200, 140, 36, 140, 120, 20, 0, 0, 20),
        })
        self.assertEqual(summary.total, _profile(
            2596, 1872, 108, 1872, 1812, 60, 0, 0, 60))

    def test_tag_filter_is_start_anchored(self) -> None:
        text = (_FIXTURES / "smaps_shell.smaps").read_text()
        self.assertIsNone(parse_smaps_text(text, re.compile(r"\.so$")))
        matched = parse_smaps_text(text, re.compile(r".*\.so"))
        assert matched is not None
        self.assertNotEqual(matched.breakdown, {})

    def test_tag_filter_matches_normalized_tag(self) -> None:
        summary = parse_smaps_text(
            "1-2 rw-p 0 00:00 0 \nSize: 4 kB\nRss: 4 kB\nPss: 4 kB\n",
            re.compile(r"\[anonymous\]"))
        assert summary is not None
        expected = MemProfile(4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        self.assertEqual(summary.breakdown, {"[anonymous]": expected})
        self.assertEqual(summary.total, expected)

    def test_tag_filter_no_match_returns_none(self) -> None:
        self.assertIsNone(parse_smaps_text(
            (_FIXTURES / "smaps_shell.smaps").read_text(),
            re.compile(r"no-such-tag")))

    def test_fixture_profiles_keep_total_invariant(self) -> None:
        fixtures = pathlib.Path(__file__).parent / "fixtures"
        for name in ("smaps_python.smaps", "smaps_self.smaps",
                     "smaps_shell.smaps"):
            with self.subTest(name=name):
                summary = parse_smaps_text(fixtures.joinpath(name).read_text())
                assert summary is not None
                for profile in [summary.total, *summary.breakdown.values()]:
                    self.assertEqual(
                        profile.shared_total_kb,
                        profile.shared_clean_kb + profile.shared_dirty_kb)
                    self.assertEqual(
                        profile.private_total_kb,
                        profile.private_clean_kb + profile.private_dirty_kb)


if __name__ == "__main__":
    unittest.main()
