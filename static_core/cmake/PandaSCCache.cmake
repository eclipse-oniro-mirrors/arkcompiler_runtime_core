# Copyright (c) 2023 Huawei Device Co., Ltd.
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

find_program(SCCACHE_FOUND sccache)
if(SCCACHE_FOUND)
    message(STATUS "SCCACHE_FOUND                          = 1")
    if(CCACHE_FOUND)
        message(STATUS "CCACHE and SCCACHE both found, using SCCACHE")
    endif(CCACHE_FOUND)
    set(CMAKE_C_COMPILER_LAUNCHER sccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER sccache)
endif(SCCACHE_FOUND)
