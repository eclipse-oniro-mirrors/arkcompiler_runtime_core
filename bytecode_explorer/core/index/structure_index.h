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

#ifndef BYTECODE_EXPLORER_CORE_INDEX_STRUCTURE_INDEX_H
#define BYTECODE_EXPLORER_CORE_INDEX_STRUCTURE_INDEX_H

#include "core/model/entity.h"

#include <array>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace panda::bytecode_explorer {

class StructureIndex final {
public:
    bool Add(EntityNode node);

    const EntityNode *Find(EntityId id) const;

    EntityNode *FindMutable(EntityId id);

    const std::vector<EntityId> &RootIds() const
    {
        return rootIds_;
    }

    size_t Count(EntityKind kind) const;

    std::vector<const EntityNode *> NodesInIdOrder() const;

    void SortChildrenDeterministically();

    size_t Size() const
    {
        return nodes_.size();
    }

    template <class Visitor>
    void ForEachNode(Visitor &&visitor) const
    {
        for (const auto &[id, node] : nodes_) {
            (void)id;
            visitor(node);
        }
    }

private:
    static constexpr size_t KIND_COUNT = static_cast<size_t>(EntityKind::COUNT);

    bool ChildOrderLess(EntityId leftId, EntityId rightId) const;

    std::unordered_map<EntityId, EntityNode> nodes_;
    std::vector<EntityId> rootIds_;
    std::array<size_t, KIND_COUNT> counts_ {};
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_INDEX_STRUCTURE_INDEX_H
