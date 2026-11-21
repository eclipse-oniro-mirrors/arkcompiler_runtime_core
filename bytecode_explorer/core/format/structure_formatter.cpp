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

#include "core/format/structure_formatter.h"

#include <sstream>

namespace panda::bytecode_explorer {
namespace {

bool IsHiddenInternalGroup(const EntityNode &node)
{
    return node.kind == EntityKind::GROUP &&
           (node.group == EntityGroup::COMPILER_GENERATED || node.group == EntityGroup::ABC_INTERNALS);
}

void AppendNode(const StructureIndex &index, const EntityNode &node, size_t depth, bool showInternals,
                std::ostringstream *output)
{
    if (!showInternals && IsHiddenInternalGroup(node)) {
        return;
    }

    *output << std::string(depth * 2U, ' ') << '[' << EntityKindName(node.kind) << "] " << node.VisibleName();
    if (node.role != EntityRole::NORMAL && node.role != EntityRole::NAMESPACE_INITIALIZER &&
        node.role != EntityRole::ENUM_INITIALIZER) {
        *output << " (" << EntityRoleName(node.role) << ')';
    }
    *output << '\n';
    for (const auto childId : node.children) {
        const auto *child = index.Find(childId);
        if (child != nullptr) {
            AppendNode(index, *child, depth + 1U, showInternals, output);
        }
    }
}

}  // namespace

std::string StructureFormatter::Format(const StructureIndex &index, bool showInternals)
{
    std::ostringstream output;
    for (const auto rootId : index.RootIds()) {
        const auto *root = index.Find(rootId);
        if (root != nullptr) {
            AppendNode(index, *root, 0, showInternals, &output);
        }
    }
    return output.str();
}

}  // namespace panda::bytecode_explorer
