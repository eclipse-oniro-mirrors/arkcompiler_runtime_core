#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

import platform
import re
from glob import glob
from os import path
from pathlib import Path
from typing import Optional, List, Union, cast

from runner import utils
from runner.common_exceptions import InvalidConfiguration
from runner.enum_types.configuration_kind import SanitizerKind, ArchitectureKind, BuildTypeKind, OSKind, \
    ConfigurationKind
from runner.enum_types.params import TestEnv
from runner.logger import Log
from runner.utils import correct_path

_LOGGER = Log.get_logger(__file__)


class TestLists:
    def __init__(self, list_root: Path, test_env: TestEnv):
        self.list_root = list_root
        self.config = test_env.config
        self.explicit_list: Optional[Path] = (
            correct_path(self.list_root, self.config.test_suite.test_lists.explicit_list)
            if self.config.test_suite.test_lists.explicit_list is not None and self.list_root is not None
            else None
        )
        self.explicit_test: Optional[Path] = None
        self.excluded_lists: List[Path] = []
        self.ignored_lists: List[Path] = []

        self.cache: List[str] = self.__cmake_cache()
        self.sanitizer = self.search_sanitizer()
        self.architecture = self.search_architecture()
        self.operating_system = self.detect_operating_system()
        self.build_type = self.search_build_type()
        self.conf_kind = self.detect_conf()

    @staticmethod
    def detect_operating_system() -> OSKind:
        system = platform.system().lower()
        if system == "linux":
            return OSKind.LIN
        if system == "windows":
            return OSKind.WIN
        return OSKind.MAC

    @staticmethod
    def __search_option_in_list(option: str, arg_list: Optional[List[str]]) -> List[str]:
        if arg_list is None:
            return []
        return [arg for arg in arg_list if arg.startswith(option)]

    @staticmethod
    def __to_bool(value: Optional[str]) -> Optional[bool]:
        true_list = ("on", "true")
        false_list = ("on", "false")
        return value in true_list if value and value in true_list + false_list else None

    def collect_excluded_test_lists(self, extra_list: Optional[List[str]] = None,
                                    test_name: Optional[str] = None) -> None:
        self.excluded_lists.extend(self.collect_test_lists("excluded", extra_list, test_name))

    def collect_ignored_test_lists(self, extra_list: Optional[List[str]] = None,
                                   test_name: Optional[str] = None) -> None:
        self.ignored_lists.extend(self.collect_test_lists("ignored", extra_list, test_name))

    def collect_test_lists(
            self,
            kind: str, extra_lists: Optional[List[str]] = None,
            test_name: Optional[str] = None
    ) -> List[Path]:
        test_lists = extra_lists[:] if extra_lists else []
        test_name = test_name if test_name else self.config.test_suite.suite_name

        short_template_name = f"{test_name}*-{kind}*.txt"
        conf_kind = self.conf_kind.value \
            if self.conf_kind != ConfigurationKind.OTHER_INT \
            else self.get_interpreter()
        full_template_name = f"{test_name}.*-{kind}" + \
                             f"(-{self.operating_system.value})?" \
                             f"(-{self.architecture.value})?" \
                             f"(-{conf_kind.upper()})?"
        if self.sanitizer != SanitizerKind.NONE:
            full_template_name += f"(-{self.sanitizer.value})?"
        full_template_name += f"(-OL{self.opt_level()})?"
        if self.debug_info():
            full_template_name += "(-DI)?"
        if self.is_full_ast_verifier():
            full_template_name += "(-FULLASTV)?"
        if self.conf_kind == ConfigurationKind.JIT and self.is_jit_with_repeats():
            full_template_name += "(-(repeats|REPEATS))?"
        full_template_name += f"(-{self.build_type.value})?"
        full_template_name += ".txt"
        full_pattern = re.compile(full_template_name)

        def is_matched(file: str) -> bool:
            file = file.split(path.sep)[-1]
            match = full_pattern.match(file)
            return match is not None

        glob_expression = path.join(self.list_root, f"**/{short_template_name}")
        test_lists.extend(filter(
            is_matched,
            glob(glob_expression, recursive=True)
        ))

        _LOGGER.all(f"Loading {kind} test lists: {test_lists}")

        return [Path(test_list) for test_list in test_lists]

    def search_build_type(self) -> BuildTypeKind:
        value = cast(str, self.__search("CMAKE_BUILD_TYPE"))
        if value == "fastverify":
            value = "fast-verify"
        return BuildTypeKind.is_value(value, option_name="from cmake CMAKE_BUILD_TYPE")

    def search_sanitizer(self) -> SanitizerKind:
        is_ubsan = self.__to_bool(self.__search("PANDA_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER"))
        is_asan = self.__to_bool(self.__search("PANDA_ENABLE_ADDRESS_SANITIZER"))
        is_tsan = self.__to_bool(self.__search("PANDA_ENABLE_THREAD_SANITIZER"))
        if is_asan or is_ubsan:
            return SanitizerKind.ASAN
        if is_tsan:
            return SanitizerKind.TSAN
        return SanitizerKind.NONE

    def search_architecture(self) -> ArchitectureKind:
        return self.config.test_suite.test_lists.architecture

    def is_aot(self) -> bool:
        aot_args = cast(Optional[list], self.config.workflow.get_value("parameters.aot-args"))
        aot_full_args = cast(Optional[list], self.config.workflow.get_value("parameters.aot-full-args"))
        return (aot_args is not None and len(aot_args) > 0) or (aot_full_args is not None and len(aot_full_args) > 0)

    def is_aot_full(self) -> bool:
        aot_args = cast(Optional[List[str]], self.config.workflow.get_value("parameters.aot-args"))
        aot_full_args = cast(Optional[List[str]], self.config.workflow.get_value("parameters.aot-full-args"))
        is_full1 = len(self.__search_option_in_list("--compiler-inline-full-intrinsics=true", aot_args)) > 0
        is_full2 = len(self.__search_option_in_list("--compiler-inline-full-intrinsics=true", aot_full_args)) > 0
        return is_full1 or is_full2

    def is_jit(self) -> bool:
        jit = str(self.config.workflow.get_parameter("compiler-enable-jit"))
        return jit.lower() == "true"

    def is_jit_with_repeats(self) -> bool:
        jit_with_repeats = cast(Optional[str], self.config.workflow.get_parameter("with-repeats"))
        return utils.to_bool(jit_with_repeats) if jit_with_repeats is not None else False

    def get_interpreter(self) -> str:
        ark_args = self.config.workflow.get_parameter("ark-args")
        is_int = self.__search_option_in_list("--interpreter-type", ark_args)
        if is_int:
            return is_int[0].split('=')[-1]
        return "int"

    def detect_conf(self) -> ConfigurationKind:
        if self.is_aot():
            if self.is_aot_full():
                return ConfigurationKind.AOT_FULL
            return ConfigurationKind.AOT

        if self.is_jit():
            return ConfigurationKind.JIT

        if self.get_interpreter() == "int":
            return ConfigurationKind.INT

        return ConfigurationKind.OTHER_INT

    def opt_level(self) -> int:
        level = str(self.config.workflow.get_parameter("opt-level"))
        return int(level)

    def debug_info(self) -> bool:
        args = self.config.workflow.get_parameter("es2panda-extra-args")
        return len(self.__search_option_in_list("--es2panda-debug-info", args)) > 0

    def is_full_ast_verifier(self) -> bool:
        args = self.config.workflow.get_parameter("es2panda-extra-args")
        return len(self.__search_option_in_list("--verifier-all-checks", args)) > 0

    def __cmake_cache(self) -> List[str]:
        cmake_cache_txt = "CMakeCache.txt"
        cmake_cache: Path = self.config.general.build / cmake_cache_txt
        if not cmake_cache.exists():
            raise InvalidConfiguration(
                f"Incorrect build folder {self.config.general.build}. Cannot find '{cmake_cache_txt}' file")
        with open(cmake_cache, "r", encoding="utf-8") as file_handler:
            cache = [line.strip()
                     for line in file_handler.readlines()
                     if line.strip() and not line.strip().startswith("#") and not line.strip().startswith("//")]
            return sorted(cache)

    def __search(self, variable: str) -> Optional[str]:
        found: List[str] = [var for var in self.cache if var.startswith(variable)]
        return str(found[0].split("=")[-1].lower()) if found else None
