# Copyright (c) 2023-2025 Huawei Device Co., Ltd.
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

panda_ets_interop_js_gtest(ets_interop_js__test_ets_proxy
    CPP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ets_proxy.cpp
    ETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ets_proxy.ets
    JS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/check_access_primitives.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_access_references.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_inheritance.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_proxy_objects.js
        ${CMAKE_CURRENT_SOURCE_DIR}/ets_proxy.test.js
    COMPILATION_JS_WITH_CJS_ON
)
