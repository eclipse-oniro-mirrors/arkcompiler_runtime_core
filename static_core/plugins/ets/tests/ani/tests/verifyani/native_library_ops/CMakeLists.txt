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

# NOTE: Enable gtest for AARCH64 when #22303 is solved.
if (PANDA_COMPILER_TARGET_AARCH64)
    return()
endif()

ani_verify_add_gtest(ani_verify_test_native_library_constructor
    CPP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/verify_native_library_constructor_test.cpp
    ETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/verify_native_library_constructor_test.ets
)

panda_add_library(verify_ani_ctor_local_scope_test SHARED verify_ani_ctor_local_scope_test.cpp)
panda_target_include_directories(verify_ani_ctor_local_scope_test PRIVATE ${PANDA_ETS_PLUGIN_SOURCE}/runtime/ani)
add_dependencies(ani_verify_test_native_library_constructor verify_ani_ctor_local_scope_test)
