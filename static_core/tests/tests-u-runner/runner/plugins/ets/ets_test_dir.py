#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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
#
# This file does only contain a selection of the most common options. For a
# full list see the documentation:
# http://www.sphinx-doc.org/en/master/config

from functools import cached_property
from pathlib import Path
from typing import Optional


class EtsTestDir:
    def __init__(self, static_core_root: str, root: Optional[str] = None) -> None:
        self.__static_core_root = Path(static_core_root)
        self.__root = root

    @cached_property
    def root(self) -> Path:
        return Path(self.__root) if self.__root else self.__static_core_root / "plugins" / "ets"

    @property
    def tests(self) -> Path:
        return self.root / "tests"

    @property
    def ets_templates(self) -> Path:
        return self.tests / "ets-templates"

    @property
    def ets_func_tests(self) -> Path:
        return self.tests / "ets_func_tests"

    @property
    def stdlib_templates(self) -> Path:
        return self.tests / "stdlib-templates"

    @property
    def gc_stress(self) -> Path:
        return self.tests / "ets_test_suite" / "gc" / "stress"
