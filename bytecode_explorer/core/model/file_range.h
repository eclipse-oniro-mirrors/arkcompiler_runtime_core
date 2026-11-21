/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef BYTECODE_EXPLORER_CORE_MODEL_FILE_RANGE_H
#define BYTECODE_EXPLORER_CORE_MODEL_FILE_RANGE_H

#include <cstdint>
#include <limits>

namespace panda::bytecode_explorer {

struct FileRange {
    uint64_t offset {0};
    uint64_t size {0};

    bool Contains(uint64_t position) const
    {
        if (size > std::numeric_limits<uint64_t>::max() - offset) {
            return false;
        }
        return position >= offset && position - offset < size;
    }

    bool IsValidFor(uint64_t fileSize) const
    {
        return offset <= fileSize && size <= fileSize - offset;
    }
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_MODEL_FILE_RANGE_H
