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

#ifndef BYTECODE_EXPLORER_CORE_READER_PHYSICAL_FALLBACK_BUILDER_H
#define BYTECODE_EXPLORER_CORE_READER_PHYSICAL_FALLBACK_BUILDER_H

#include "core/reader/hierarchy_builder.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace panda::bytecode_explorer {

struct PhysicalFallbackContext {
    const PhysicalRecord *record {nullptr};
    EntityId parentId {INVALID_ENTITY_ID};
    const std::unordered_map<uint32_t, std::string> *methodDiagnostics {nullptr};
};

class PhysicalFallbackBuilder final {
public:
    PhysicalFallbackBuilder(const PhysicalProgram &program, HierarchyBuilder *hierarchy);

    bool Build(const std::vector<PhysicalFallbackContext> &contexts);

private:
    bool AddFields(const PhysicalRecord &record, EntityId groupId);
    bool AddMethods(const PhysicalRecord &record, const PhysicalFallbackContext &context, EntityId groupId);
    std::map<std::string, std::vector<const PhysicalMethod *>> CollectMethodBatches(
        const PhysicalRecord &record, const PhysicalFallbackContext &context) const;
    bool AppendMethodBatch(const std::string &reason, const std::vector<const PhysicalMethod *> &methods,
                           EntityId groupId);
    bool AddLiteralArrays();
    bool AddPhysical(EntityId parentId, PhysicalEntityKind kind, uint64_t offset, uint64_t size,
                     const std::string &name);

    const PhysicalProgram &program_;
    HierarchyBuilder *hierarchy_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_PHYSICAL_FALLBACK_BUILDER_H
