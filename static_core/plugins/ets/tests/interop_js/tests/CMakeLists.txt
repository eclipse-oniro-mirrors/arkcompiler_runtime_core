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

set(DISABLES_TESTS
    standalone_function
)

SUBDIRLIST(EXECUTED_TESTS
    ${CMAKE_CURRENT_LIST_DIR}
    DISABLES_TESTS
)

foreach(TEST_DIR ${EXECUTED_TESTS})
    add_subdirectory(${TEST_DIR})
endforeach()
