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

#include "core/index/structure_index.h"

#include <algorithm>
#include <utility>

namespace panda::bytecode_explorer {

bool StructureIndex::Add(EntityNode node)
{
    if (node.id == INVALID_ENTITY_ID || nodes_.find(node.id) != nodes_.end()) {
        return false;
    }
    if (node.parentId != INVALID_ENTITY_ID && nodes_.find(node.parentId) == nodes_.end()) {
        return false;
    }

    const EntityId id = node.id;
    const EntityId parentId = node.parentId;
    const auto kindIndex = static_cast<size_t>(node.kind);
    if (kindIndex >= KIND_COUNT) {
        return false;
    }

    nodes_.emplace(id, std::move(node));
    if (parentId == INVALID_ENTITY_ID) {
        rootIds_.push_back(id);
    } else {
        nodes_.at(parentId).children.push_back(id);
    }
    ++counts_[kindIndex];
    return true;
}

const EntityNode *StructureIndex::Find(EntityId id) const
{
    auto iterator = nodes_.find(id);
    return iterator == nodes_.end() ? nullptr : &iterator->second;
}

EntityNode *StructureIndex::FindMutable(EntityId id)
{
    auto iterator = nodes_.find(id);
    return iterator == nodes_.end() ? nullptr : &iterator->second;
}

size_t StructureIndex::Count(EntityKind kind) const
{
    const auto kindIndex = static_cast<size_t>(kind);
    return kindIndex < KIND_COUNT ? counts_[kindIndex] : 0;
}

std::vector<const EntityNode *> StructureIndex::NodesInIdOrder() const
{
    std::vector<const EntityNode *> nodes;
    nodes.reserve(nodes_.size());
    for (const auto &[id, node] : nodes_) {
        nodes.push_back(&node);
    }
    std::sort(nodes.begin(), nodes.end(), [](const auto *left, const auto *right) { return left->id < right->id; });
    return nodes;
}

// Sort-priority bands for entity kinds: containers first, then declarations, then ABC internals.
constexpr int PRIORITY_DOCUMENT_SCOPE = 0;
constexpr int PRIORITY_TYPE_DECLARATION = 10;
constexpr int PRIORITY_CALLABLE = 20;
constexpr int PRIORITY_DATA_MEMBER = 30;
constexpr int PRIORITY_ANNOTATION_USE = 40;
constexpr int PRIORITY_MODULE_LINK = 50;
constexpr int PRIORITY_ABC_INTERNAL = 60;
constexpr int PRIORITY_GROUP_BASE = 100;
constexpr int PRIORITY_UNPLACED = 200;

int EntityPriority(const EntityNode &node)
{
    if (node.kind == EntityKind::GROUP) {
        return PRIORITY_GROUP_BASE + static_cast<int>(node.group);
    }
    switch (node.kind) {
        case EntityKind::FILE:
        case EntityKind::MODULE:
        case EntityKind::SCRIPT:
        case EntityKind::NAMESPACE:
            return PRIORITY_DOCUMENT_SCOPE;
        case EntityKind::CLASS:
        case EntityKind::INTERFACE:
        case EntityKind::ENUM:
        case EntityKind::ANNOTATION_INTERFACE:
            return PRIORITY_TYPE_DECLARATION;
        case EntityKind::FUNCTION:
        case EntityKind::METHOD:
            return PRIORITY_CALLABLE;
        case EntityKind::FIELD:
        case EntityKind::ENUM_MEMBER:
            return PRIORITY_DATA_MEMBER;
        case EntityKind::ANNOTATION:
            return PRIORITY_ANNOTATION_USE;
        case EntityKind::IMPORT:
        case EntityKind::EXPORT:
            return PRIORITY_MODULE_LINK;
        case EntityKind::ABC_ITEM:
            return PRIORITY_ABC_INTERNAL;
        case EntityKind::GROUP:
        case EntityKind::COUNT:
            return PRIORITY_UNPLACED;
    }
    return PRIORITY_UNPLACED;
}

// Orders two children of one node: by kind priority, then primary offset, then name, then id.
bool StructureIndex::ChildOrderLess(EntityId leftId, EntityId rightId) const
{
    const auto &left = nodes_.at(leftId);
    const auto &right = nodes_.at(rightId);
    const auto leftPriority = EntityPriority(left);
    const auto rightPriority = EntityPriority(right);
    if (leftPriority != rightPriority) {
        return leftPriority < rightPriority;
    }
    const auto leftOffset = left.primaryRange.has_value() ? left.primaryRange->offset : UINT64_MAX;
    const auto rightOffset = right.primaryRange.has_value() ? right.primaryRange->offset : UINT64_MAX;
    if (leftOffset != rightOffset) {
        return leftOffset < rightOffset;
    }
    if (left.name != right.name) {
        return left.name < right.name;
    }
    return left.id < right.id;
}

void StructureIndex::SortChildrenDeterministically()
{
    for (auto &[id, node] : nodes_) {
        std::stable_sort(node.children.begin(), node.children.end(),
                         [this](EntityId leftId, EntityId rightId) { return ChildOrderLess(leftId, rightId); });
    }
}

const char *EntityKindName(EntityKind kind)
{
    switch (kind) {
        case EntityKind::FILE:
            return "File";
        case EntityKind::MODULE:
            return "Module";
        case EntityKind::SCRIPT:
            return "Script";
        case EntityKind::GROUP:
            return "Group";
        case EntityKind::NAMESPACE:
            return "Namespace";
        case EntityKind::CLASS:
            return "Class";
        case EntityKind::INTERFACE:
            return "Interface";
        case EntityKind::ENUM:
            return "Enum";
        case EntityKind::ANNOTATION_INTERFACE:
            return "Annotation Interface";
        case EntityKind::FUNCTION:
            return "Function";
        case EntityKind::METHOD:
            return "Method";
        case EntityKind::FIELD:
            return "Field";
        case EntityKind::ENUM_MEMBER:
            return "Enum Member";
        case EntityKind::IMPORT:
            return "Import";
        case EntityKind::EXPORT:
            return "Export";
        case EntityKind::ANNOTATION:
            return "Annotation";
        case EntityKind::ABC_ITEM:
            return "ABC Item";
        case EntityKind::COUNT:
            return "Unknown";
    }
    return "Unknown";
}

const char *EntityRoleName(EntityRole role)
{
    switch (role) {
        case EntityRole::NORMAL:
            return "Normal";
        case EntityRole::CONSTRUCTOR:
            return "Constructor";
        case EntityRole::INSTANCE_METHOD:
            return "Instance Method";
        case EntityRole::STATIC_METHOD:
            return "Static Method";
        case EntityRole::GETTER:
            return "Getter";
        case EntityRole::SETTER:
            return "Setter";
        case EntityRole::MODULE_INITIALIZER:
            return "Module Initializer";
        case EntityRole::NAMESPACE_INITIALIZER:
            return "Namespace Initializer";
        case EntityRole::ENUM_INITIALIZER:
            return "Enum Initializer";
        case EntityRole::COMPILER_GENERATED:
            return "Compiler Generated";
        case EntityRole::ABC_INTERNAL:
            return "ABC Internal";
    }
    return "Unknown";
}

const char *EntityOriginName(EntityOrigin origin)
{
    switch (origin) {
        case EntityOrigin::PHYSICAL:
            return "Physical";
        case EntityOrigin::RECONSTRUCTED:
            return "Reconstructed";
        case EntityOrigin::INFERRED:
            return "Inferred";
    }
    return "Unknown";
}

const char *PhysicalEntityKindName(PhysicalEntityKind kind)
{
    switch (kind) {
        case PhysicalEntityKind::FILE:
            return "File";
        case PhysicalEntityKind::RECORD:
            return "Record";
        case PhysicalEntityKind::METHOD:
            return "Method";
        case PhysicalEntityKind::FIELD:
            return "Field";
        case PhysicalEntityKind::LITERAL_ARRAY:
            return "Literal Array";
        case PhysicalEntityKind::CODE:
            return "Code";
        case PhysicalEntityKind::ANNOTATION:
            return "Annotation";
        case PhysicalEntityKind::MODULE_DATA:
            return "Module Data";
    }
    return "Unknown";
}

}  // namespace panda::bytecode_explorer
