# Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

cmake_minimum_required(VERSION 3.10)

project(ark)

set(SOURCES
    panda.cpp
)

panda_add_executable(ark ${SOURCES})

panda_target_include_directories(ark
    PUBLIC ../runtime
    PUBLIC ${CMAKE_BINARY_DIR}/libpandabase
)

panda_target_link_libraries(ark arkruntime arkbase)

panda_add_sanitizers(TARGET ark SANITIZERS ${PANDA_SANITIZERS_LIST})

# TODO: remove after all components will use ark instead of panda
add_custom_target(panda ALL
                  COMMAND cd $<TARGET_FILE_DIR:ark> && ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:ark> panda)

add_dependencies(panda ark)
