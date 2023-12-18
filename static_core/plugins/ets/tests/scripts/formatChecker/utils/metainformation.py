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

# This file defines the CTS test metadata format
# The entrypoint is the 'find_all_metas' function

from typing import Tuple, List
import yaml
import re

from utils.constants import META_END_PATTERN, META_END_STRING, META_START_PATTERN, META_START_STRING


ParsedMeta = dict 
MetaInText = Tuple[int, int, ParsedMeta]


class InvalidMetaException(Exception):
    def __init__(self, msg: str) -> None:
        super().__init__()
        self.message = msg

    
def find_all_metas(text: str) -> List[MetaInText]:
    """
    Given a text of the whole test, this function:
    1) Find all metas in this text
    2) Parses each meta and verifies it's correctness 
    Returns a list of tuples (start: int, end: int, meta: ParsedMeta),
    where 'start' and 'end' are indices in 'text' such that text[start:end] == "/*---" + strMeta + "---*/"
    and 'meta' is the parsed meta   
    
    Raised: InvalidMetaException 
    """
    result = []

    start_indices = [m.start() for m in re.finditer(META_START_PATTERN, text)]
    end_indices = [m.end() for m in re.finditer(META_END_PATTERN, text)]


    if len(start_indices) != len(end_indices) or len(start_indices) == 0:
        raise InvalidMetaException("Invalid meta or meta doesn't exist")
    
    meta_bounds = list(zip(start_indices, end_indices))

    # verify meta bounds
    for i in range(1, len(meta_bounds)):
        prev_start, prev_end = meta_bounds[i - 1]
        start, end = meta_bounds[i]
        if not (prev_start < prev_end < start < end):
            raise InvalidMetaException("Invalid meta")

    for start, end in meta_bounds:
        meta = __parse_meta(text[start:end])
        result.append((start, end, meta))

    return result


def __parse_meta(meta: str) -> dict:
    """
    Given a meta, a string that starts with '/*---', ends with '---*/' and contains a valid YAML in between,
    this function parses that meta and validating it.
    """
    assert len(meta) > len(META_START_STRING) + len(META_END_STRING)
    assert meta.startswith(META_START_STRING) and meta.endswith(META_END_STRING)

    yaml_string = meta[len(META_START_STRING):-len(META_END_STRING)]
    try:
        return yaml.load(yaml_string, Loader=yaml.Loader)
    except Exception as e:
        raise InvalidMetaException(str(e))
