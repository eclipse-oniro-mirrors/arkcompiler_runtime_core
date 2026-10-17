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

from src.errors import CombinedError


class CombinedErrorTest(unittest.TestCase):
    def test_empty(self) -> None:
        errors = CombinedError()
        self.assertIsNone(errors.get())
        errors.raise_if_any()

    def test_first_error_is_raised_from_itself(self) -> None:
        errors = CombinedError()
        original = RuntimeError("first")
        errors.add(original, "ignored")

        self.assertIs(errors.get(), original)
        with self.assertRaises(RuntimeError) as raised:
            errors.raise_if_any()
        self.assertIs(raised.exception, original)
        self.assertIs(raised.exception.__cause__, original)

    def test_later_error_is_combined_with_context(self) -> None:
        errors = CombinedError()
        original = RuntimeError("first")
        errors.add(original, None)
        errors.add(RuntimeError("second"), "cleanup")

        self.assertEqual(str(errors.get()),
                         "first; additionally cleanup: second")
        with self.assertRaisesRegex(RuntimeError, "additionally cleanup: second") as raised:
            errors.raise_if_any()
        self.assertIs(raised.exception.__cause__, original)

    def test_later_error_without_context(self) -> None:
        errors = CombinedError()
        errors.add(RuntimeError("first"), None)
        errors.add(RuntimeError("second"), None)

        self.assertEqual(str(errors.get()), "first; additionally second")
