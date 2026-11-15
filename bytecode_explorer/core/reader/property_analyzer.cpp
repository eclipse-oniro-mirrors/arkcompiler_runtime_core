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

#include "core/reader/property_analyzer.h"

#include <limits>
#include <set>

namespace panda::bytecode_explorer {

std::vector<PropertyEvidence> PropertyAnalyzer::FindStoredProperties(const PhysicalMethod &initializer,
                                                                     uint32_t receiverArgumentIndex, bool isStatic)
{
    std::vector<PropertyEvidence> properties;
    std::set<std::string> names;
    const uint32_t receiver = initializer.numVregs + receiverArgumentIndex;
    if (receiver > std::numeric_limits<uint16_t>::max() || receiverArgumentIndex >= initializer.numArgs) {
        return properties;
    }

    const auto expectedKind =
        isStatic ? BytecodeEvidenceKind::DEFINE_PROPERTY_BY_NAME : BytecodeEvidenceKind::STORE_PROPERTY_BY_NAME;
    for (const auto &evidence : initializer.evidence) {
        if (evidence.kind != expectedKind || evidence.registerCount == 0U || evidence.registers.front() != receiver ||
            evidence.text.empty() || !names.emplace(evidence.text).second) {
            continue;
        }
        properties.push_back({evidence.text, isStatic, evidence.range});
    }
    return properties;
}

std::unordered_map<uint32_t, EntityRole> PropertyAnalyzer::ClassifyAccessorPair(
    const std::vector<const PhysicalMethod *> &methods)
{
    std::unordered_map<uint32_t, EntityRole> roles;
    if (methods.size() != 2U || methods[0] == nullptr || methods[1] == nullptr ||
        methods[0]->numArgs == methods[1]->numArgs) {
        return roles;
    }
    const auto *getter = methods[0]->numArgs < methods[1]->numArgs ? methods[0] : methods[1];
    const auto *setter = getter == methods[0] ? methods[1] : methods[0];
    if (setter->numArgs != getter->numArgs + 1U) {
        return roles;
    }
    roles.emplace(getter->offset, EntityRole::GETTER);
    roles.emplace(setter->offset, EntityRole::SETTER);
    return roles;
}

}  // namespace panda::bytecode_explorer
