#!/usr/bin/env python3
# -*- coding: utf-8 -*-

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


from typing import Any, Dict, Optional
from pydantic import BaseModel, Field
from .common import ResponseLog, DisasmResponse


class CompileRequestModel(BaseModel):
    code: str
    options: Dict[str, Any] = Field(default_factory=dict)
    disassemble: bool
    runtime_verify: bool = False


class RunResponse(BaseModel):
    run: Optional[ResponseLog] = None
    compile: ResponseLog
    disassembly: Optional[DisasmResponse] = None


class CompileResponse(BaseModel):
    compile: ResponseLog
    disassembly: Optional[DisasmResponse] = None
