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


class CombinedError:
    def __init__(self) -> None:
        self._original: Exception | None = None
        self._combined: Exception | None = None

    def add(self, exc: Exception, context: str | None) -> None:
        if self._original is None:
            self._original = exc
            self._combined = exc
            return
        if self._combined is None:
            raise Exception("CombinedError invariant failure")
        self._combined = self._combine_errors(self._combined, exc, context)

    def get(self) -> Exception | None:
        return self._combined

    def raise_if_any(self) -> None:
        if self._original is None:
            return
        if self._combined is None:
            raise Exception("CombinedError invariant failure")
        raise self._combined from self._original

    def _combine_errors(
        self,
        primary: Exception,
        secondary: Exception,
        context: str | None,
    ) -> Exception:
        if context is None:
            return RuntimeError(f"{primary}; additionally {secondary}")
        return RuntimeError(f"{primary}; additionally {context}: {secondary}")
