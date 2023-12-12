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
from runner.options.options_general import GeneralOptions


class CoverageDir:
    def __init__(self, general: GeneralOptions, work_dir: Path):
        self.__general = general
        self.__work_dir = work_dir

    @cached_property
    def root(self) -> Path:
        root = self.__work_dir / "coverage"
        root.mkdir(parents=True, exist_ok=True)
        return root

    @property
    def html_report_dir(self) -> Path:
        if self.__general.coverage.llvm_cov_html_out_path:
            html_out_path = Path(self.__general.coverage.llvm_cov_html_out_path)
        else:
            html_out_path = self.root / "html_report_dir"
        html_out_path.mkdir(parents=True, exist_ok=True)
        return html_out_path

    @cached_property
    def profdata_dir(self) -> Path:
        if self.__general.coverage.llvm_profdata_out_path:
            profdata_out_path = Path(self.__general.coverage.llvm_profdata_out_path)
        else:
            profdata_out_path = self.root / "profdata_dir"
        profdata_out_path.mkdir(parents=True, exist_ok=True)
        return profdata_out_path

    @cached_property
    def coverage_work_dir(self) -> Path:
        work_dir = self.root / "work_dir"
        work_dir.mkdir(parents=True, exist_ok=True)
        return work_dir

    @property
    def info_file(self) -> Path:
        return self.coverage_work_dir / "coverage_lcov_format.info"

    @property
    def profdata_files_list_file(self) -> Path:
        return self.coverage_work_dir / "profdatalist.txt"

    @property
    def profdata_merged_file(self) -> Path:
        return self.coverage_work_dir / "merged.profdata"
