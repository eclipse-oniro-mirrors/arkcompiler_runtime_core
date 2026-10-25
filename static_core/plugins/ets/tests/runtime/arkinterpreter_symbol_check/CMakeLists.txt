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

# Only run with ETS plugin
if (NOT PANDA_WITH_ETS)
    return()
endif()

# Path to the built libarkinterpreter.so
set(ARKINTERPRETER_SO_PATH "$<TARGET_FILE:arkinterpreter>")

panda_ets_add_gtest(
    NO_CORES
    NAME arkinterpreter_symbol_check_test
    CUSTOM_PRERUN_ENVIRONMENT
        "ARKINTERPRETER_SO_PATH=${ARKINTERPRETER_SO_PATH}"
    SOURCES
        arkinterpreter_symbol_test.cpp
    LIBRARIES
        ${CMAKE_DL_LIBS}
    SANITIZERS
        ${PANDA_SANITIZERS_LIST}
    DEPS_TARGETS
        arkinterpreter
)

# elfio is a header-only library; just add include path (not a link target)
panda_target_include_directories(arkinterpreter_symbol_check_test SYSTEM
    PUBLIC ${PANDA_THIRD_PARTY_SOURCES_DIR}/elfio)
