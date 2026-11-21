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

#ifndef BYTECODE_EXPLORER_CORE_READER_MODULE_BINDING_ANALYZER_H
#define BYTECODE_EXPLORER_CORE_READER_MODULE_BINDING_ANALYZER_H

#include "core/model/entity.h"
#include "core/model/physical_program.h"

#include <optional>
#include <string>

namespace panda::bytecode_explorer {

struct ModuleBindingPresentation {
    EntityKind kind {EntityKind::IMPORT};
    EntityGroup group {EntityGroup::IMPORTS};
    std::string name;
    std::string displayName;
    std::string details;
    std::optional<std::string> diagnostic;
};

class ModuleBindingAnalyzer final {
public:
    static ModuleBindingPresentation Analyze(const ModuleBinding &binding);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_MODULE_BINDING_ANALYZER_H
