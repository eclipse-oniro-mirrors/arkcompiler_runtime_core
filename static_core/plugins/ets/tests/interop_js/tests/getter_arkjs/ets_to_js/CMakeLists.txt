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

set(ETS_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/arktsconfig.json)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/arktsconfig.in.json ${ETS_CONFIG})

panda_ets_interop_js_gtest(ets_interop_js__getter_ets_to_js
    CPP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/getter.cpp
    ETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/getter.ets
    ETS_CONFIG ${ETS_CONFIG}
    JS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/check_any_type_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_any_type_getter_class_tuple.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_private_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_private_getter_inheritance_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_protected_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_protected_getter_inheritance_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_public_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_subset_by_ref_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_subset_by_value_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_union_type_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/check_static_getter_class.js
        ${CMAKE_CURRENT_SOURCE_DIR}/constant.js
)
