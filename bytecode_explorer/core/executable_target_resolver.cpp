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

#include "core/executable_target_resolver.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

bool IsExecutable(const EntityNode &node)
{
    switch (node.kind) {
        case EntityKind::FUNCTION:
        case EntityKind::METHOD:
        case EntityKind::ABC_ITEM:
            return true;
        case EntityKind::NAMESPACE:
            return node.role == EntityRole::NAMESPACE_INITIALIZER;
        case EntityKind::ENUM:
            return node.role == EntityRole::ENUM_INITIALIZER;
        default:
            return false;
    }
}

struct Candidate {
    std::string physicalName;
};

struct CodeMatch {
    FileRange range;
    size_t count {0U};
};

}  // namespace

std::vector<ExecutableTarget> ExecutableTargetResolver::Resolve(const EntityNode &node)
{
    if (!IsExecutable(node)) {
        return {};
    }

    std::map<uint64_t, Candidate> candidates;
    std::map<std::string, CodeMatch> codeMatches;
    for (const auto &reference : node.backing) {
        if (reference.kind == PhysicalEntityKind::METHOD) {
            auto &[physicalName] = candidates[reference.range.offset];
            if (!reference.name.empty() && (physicalName.empty() || reference.name < physicalName)) {
                physicalName = reference.name;
            }
        } else if (reference.kind == PhysicalEntityKind::CODE && !reference.name.empty()) {
            auto &[range, count] = codeMatches[reference.name];
            range = reference.range;
            ++count;
        }
    }

    std::vector<ExecutableTarget> targets;
    targets.reserve(candidates.size());
    std::map<std::string, size_t> methodNameCounts;
    for (const auto &[methodOffset, candidate] : candidates) {
        static_cast<void>(methodOffset);
        if (!candidate.physicalName.empty()) {
            ++methodNameCounts[candidate.physicalName];
        }
    }
    for (auto &[methodOffset, candidate] : candidates) {
        ExecutableTarget target;
        target.entityId = node.id;
        target.methodOffset = methodOffset;
        if (candidate.physicalName.empty()) {
            target.methodName = node.VisibleName();
        } else {
            target.methodName = std::move(candidate.physicalName);
            const auto code = codeMatches.find(target.methodName);
            if (code != codeMatches.end() && code->second.count == 1U && methodNameCounts[target.methodName] == 1U) {
                target.codeRange = code->second.range;
            }
        }
        targets.push_back(std::move(target));
    }
    return targets;
}

}  // namespace panda::bytecode_explorer
