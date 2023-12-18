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
from typing import Dict, Optional

from runner.options.decorator_value import value, _to_int, _to_str, _to_path


class Es2PandaOptions:
    __DEFAULT_TIMEOUT = 60
    __DEFAULT_OPT_LEVEL = 2

    def __str__(self) -> str:
        return _to_str(self, 1)

    def to_dict(self) -> Dict[str, object]:
        return {
            "timeout": self.timeout,
            "opt-level": self.opt_level,
            "custom-path": self.custom_path,
            "arktsconfig": self.arktsconfig,
        }

    @cached_property
    @value(yaml_path="es2panda.timeout", cli_name="es2panda_timeout", cast_to_type=_to_int)
    def timeout(self) -> int:
        return Es2PandaOptions.__DEFAULT_TIMEOUT

    @cached_property
    @value(yaml_path="es2panda.opt-level", cli_name="es2panda_opt_level", cast_to_type=_to_int)
    def opt_level(self) -> int:
        return Es2PandaOptions.__DEFAULT_OPT_LEVEL

    @cached_property
    @value(yaml_path="es2panda.custom-path", cli_name="custom_es2panda_path", cast_to_type=_to_path)
    def custom_path(self) -> Optional[str]:
        return None

    @cached_property
    @value(yaml_path="es2panda.arktsconfig", cli_name="arktsconfig", cast_to_type=_to_path)
    def arktsconfig(self) -> Optional[str]:
        return None

    def get_command_line(self) -> str:
        options = [
            f'--es2panda-timeout={self.timeout}' if self.timeout != Es2PandaOptions.__DEFAULT_TIMEOUT else '',
            f'--es2panda-opt-level={self.opt_level}' if self.opt_level != Es2PandaOptions.__DEFAULT_OPT_LEVEL else '',
            f'--custom-es2panda="{self.custom_path}"' if self.custom_path is not None else '',
            f'--arktsconfig="{self.arktsconfig}"' if self.arktsconfig is not None else '',
        ]
        return ' '.join(options)
