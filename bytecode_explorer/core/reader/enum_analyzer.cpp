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

#include "core/reader/enum_analyzer.h"

#include <algorithm>
#include <set>

namespace panda::bytecode_explorer {

std::vector<EnumMemberEvidence> EnumAnalyzer::RecoverMembers(const PhysicalMethod &initializer)
{
    std::vector<EnumMemberEvidence> members;
    std::set<std::string> names;
    for (size_t index = 0; index + 1U < initializer.evidence.size(); ++index) {
        const auto &load = initializer.evidence[index];
        const auto &saveKey = initializer.evidence[index + 1U];
        if (load.kind != BytecodeEvidenceKind::LOAD_STRING || load.text.empty() ||
            saveKey.kind != BytecodeEvidenceKind::STORE_ACCUMULATOR || saveKey.registerCount == 0U) {
            continue;
        }

        const auto keyRegister = saveKey.registers.front();
        bool usedAsKey = false;
        for (size_t candidate = index + 2U; candidate < initializer.evidence.size(); ++candidate) {
            const auto &evidence = initializer.evidence[candidate];
            const auto end = evidence.registers.begin() + evidence.registerCount;
            if (evidence.kind == BytecodeEvidenceKind::STORE_BY_VALUE &&
                std::find(evidence.registers.begin(), end, keyRegister) != end) {
                usedAsKey = true;
                break;
            }
            if (evidence.kind == BytecodeEvidenceKind::STORE_ACCUMULATOR && evidence.registerCount > 0U &&
                evidence.registers.front() == keyRegister) {
                break;
            }
        }
        if (usedAsKey && names.emplace(load.text).second) {
            members.push_back({load.text, load.range});
        }
    }
    return members;
}

}  // namespace panda::bytecode_explorer
