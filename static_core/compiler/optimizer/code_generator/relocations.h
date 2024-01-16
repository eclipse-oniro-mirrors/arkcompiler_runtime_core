/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PANDA_RELOCATIONS_H
#define PANDA_RELOCATIONS_H

#include <cstdint>

namespace ark::compiler {

class RelocationInfo {
public:
    uint32_t data {0};
    uint32_t offset {0};
    uint32_t type {0};
    int8_t addend {0};
};

class RelocationHandler {
public:
    virtual void AddRelocation(const RelocationInfo &info) = 0;
};

}  // namespace ark::compiler

#endif  // PANDA_RELOCATIONS_H
