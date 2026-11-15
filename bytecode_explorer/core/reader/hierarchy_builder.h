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

#ifndef BYTECODE_EXPLORER_CORE_READER_HIERARCHY_BUILDER_H
#define BYTECODE_EXPLORER_CORE_READER_HIERARCHY_BUILDER_H

#include "core/index/structure_index.h"
#include "core/model/physical_program.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace panda::bytecode_explorer {

class HierarchyBuilder final {
public:
    HierarchyBuilder(std::string path, const PhysicalProgram &program);

    bool Initialize();
    void Finalize();
    bool AddNode(EntityNode node);
    EntityId EnsureGroup(EntityId parentId, EntityGroup group);
    void AddBacking(EntityNode *node, PhysicalEntityKind kind, uint64_t offset, uint64_t size, const std::string &name);
    void AddReference(EntityNode *node, PhysicalEntityKind kind, FileRange range, const std::string &name);
    void AddCodeBacking(EntityNode *node, const PhysicalMethod &method);
    bool AddAnnotations(EntityId parentId, const std::vector<PhysicalAnnotation> &annotations);

    bool IsConsumed(PhysicalEntityKind kind, uint64_t offset) const;
    EntityNode *Mutable(EntityId id);
    EntityId NextId();

    EntityId RootId() const
    {
        return rootId_;
    }

    const std::shared_ptr<StructureIndex> &Index() const
    {
        return index_;
    }

    const PhysicalProgram &Program() const
    {
        return program_;
    }

    const std::string &Error() const
    {
        return error_;
    }

private:
    static const char *GroupName(EntityGroup group);
    bool Fail(std::string message);
    void Consume(PhysicalEntityKind kind, uint64_t offset);

    std::string path_;
    const PhysicalProgram &program_;
    std::shared_ptr<StructureIndex> index_;
    std::map<std::pair<EntityId, EntityGroup>, EntityId> groups_;
    std::set<std::pair<PhysicalEntityKind, uint64_t>> consumed_;
    EntityId rootId_ {INVALID_ENTITY_ID};
    EntityId nextId_ {1};
    std::string error_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_HIERARCHY_BUILDER_H
