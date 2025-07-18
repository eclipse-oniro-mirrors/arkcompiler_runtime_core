/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_STDLIB_NATIVE_ESCOMPAT_REGEXP_REGEXP_EXEC_RESULT_H
#define PANDA_PLUGINS_ETS_STDLIB_NATIVE_ESCOMPAT_REGEXP_REGEXP_EXEC_RESULT_H

#include <map>
#include <string>
#include <vector>

#include <cstdint>

namespace ark::ets::stdlib {

struct RegExpExecResult {
    using IndexPair = std::pair<uint32_t, uint32_t>;
    uint32_t index = 0;
    uint32_t endIndex = 0;
    std::vector<IndexPair> indices;
    std::map<std::string, std::pair<int32_t, int32_t>> namedGroups;
    bool isSuccess = false;
    bool isWide = false;
};

}  // namespace ark::ets::stdlib

#endif  // PANDA_PLUGINS_ETS_STDLIB_NATIVE_ESCOMPAT_REGEXP_REGEXP_EXEC_RESULT_H
