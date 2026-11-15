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

#ifndef BYTECODE_EXPLORER_CORE_READER_DYNAMIC_NAME_PARSER_H
#define BYTECODE_EXPLORER_CORE_READER_DYNAMIC_NAME_PARSER_H

#include "core/model/entity.h"

#include <optional>
#include <string>
#include <vector>

namespace panda::bytecode_explorer {

struct ParsedDynamicName {
    EntityKind kind {EntityKind::FUNCTION};
    EntityRole role {EntityRole::NORMAL};
    std::string name;
    std::string internalName;
    std::vector<std::string> parents;
    std::vector<std::string> parentKeys;
    std::vector<EntityKind> parentKinds;
    bool reliable {true};
    std::string diagnostic;
};

class DynamicNameParser final {
public:
    static std::optional<ParsedDynamicName> Parse(const std::string &moduleName, const std::string &internalName,
                                                  const std::vector<std::string> &scopeNames);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_DYNAMIC_NAME_PARSER_H
