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

project(verifier)

set(SOURCES verifier.cpp)

panda_add_executable(verifier ${SOURCES})

panda_target_include_directories(verifier PUBLIC ${PANDA_ROOT}/libpandabase/
                                                 ${PANDA_ROOT}/runtime/
                                                 ${PANDA_BINARY_ROOT}
                                                 ${VERIFIER_INCLUDE_DIR}
)

panda_target_link_libraries(verifier arkruntime arkbase)

panda_add_sanitizers(TARGET verifier SANITIZERS ${PANDA_SANITIZERS_LIST})

panda_gen_options(TARGET verifier YAML_FILE options.yaml GENERATED_HEADER
                  verifier_options_gen.h)
