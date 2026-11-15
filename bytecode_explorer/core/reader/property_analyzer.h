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

#ifndef BYTECODE_EXPLORER_CORE_READER_PROPERTY_ANALYZER_H
#define BYTECODE_EXPLORER_CORE_READER_PROPERTY_ANALYZER_H

#include "core/model/entity.h"
#include "core/model/physical_program.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace panda::bytecode_explorer {

struct PropertyEvidence {
    std::string name;
    bool isStatic {false};
    FileRange range;
};

class PropertyAnalyzer final {
public:
    static std::vector<PropertyEvidence> FindStoredProperties(const PhysicalMethod &initializer,
                                                              uint32_t receiverArgumentIndex, bool isStatic);

    static std::unordered_map<uint32_t, EntityRole> ClassifyAccessorPair(
        const std::vector<const PhysicalMethod *> &methods);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_PROPERTY_ANALYZER_H
