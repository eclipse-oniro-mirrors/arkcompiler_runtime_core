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

from enum import Enum
from typing import TypeVar, Type, Optional, Any, List, NoReturn

from runner.common_exceptions import IncorrectEnumValue

EnumT = TypeVar("EnumT", bound='BaseEnum')


class BaseEnum(Enum):

    @classmethod
    def raise_incorrect_value(cls: Type[EnumT], value: str, *, reason: str = '') -> NoReturn:
        raise IncorrectEnumValue(
            f'Incorrect value "{value}" for {reason if reason else cls.__name__}'
            f'Expected one of: {cls.values()}')

    @classmethod
    def from_str(cls: Type[EnumT], item_name: str) -> Optional[EnumT]:
        for enum_value in cls:
            if enum_value.value.lower() == item_name.lower() or str(enum_value).lower() == item_name.lower():
                return enum_value
        return None

    @classmethod
    def values(cls: Type[EnumT]) -> List[Any]:
        return [cls[attr].value for attr in cls._member_map_]

    @classmethod
    def is_value(cls: Type[EnumT], value: str, option_name: str) -> EnumT:
        enum_value = cls.from_str(value)
        if enum_value is None:
            cls.raise_incorrect_value(value, reason=f"parameter {option_name}")
        return enum_value
