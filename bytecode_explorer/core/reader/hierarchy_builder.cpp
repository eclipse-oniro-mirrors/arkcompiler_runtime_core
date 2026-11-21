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

#include "core/reader/hierarchy_builder.h"

#include "core/reader/annotation_analyzer.h"

#include <filesystem>
#include <optional>
#include <sstream>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

std::optional<FileRange> RangeFor(uint64_t offset, uint64_t size, uint64_t fileSize)
{
    FileRange range {offset, size};
    return range.IsValidFor(fileSize) ? std::make_optional(range) : std::nullopt;
}

}  // namespace

HierarchyBuilder::HierarchyBuilder(std::string path, const PhysicalProgram &program)
    : path_(std::move(path)), program_(program), index_(std::make_shared<StructureIndex>())
{
}

bool HierarchyBuilder::Initialize()
{
    EntityNode root(NextId(), INVALID_ENTITY_ID, EntityKind::FILE, std::filesystem::path(path_).filename().string());
    root.internalName = path_;
    root.primaryRange = RangeFor(0U, program_.fileSize, program_.fileSize);
    if (root.primaryRange.has_value()) {
        AddReference(&root, PhysicalEntityKind::FILE, *root.primaryRange, root.name);
    }
    rootId_ = root.id;
    return AddNode(std::move(root));
}

void HierarchyBuilder::Finalize()
{
    index_->SortChildrenDeterministically();
}

bool HierarchyBuilder::AddNode(EntityNode node)
{
    if (node.kind == EntityKind::MODULE || node.kind == EntityKind::NAMESPACE || node.kind == EntityKind::CLASS ||
        node.kind == EntityKind::INTERFACE || node.kind == EntityKind::ENUM ||
        node.kind == EntityKind::ANNOTATION_INTERFACE) {
        node.defaultCollapsed = true;
    }
    const bool validRoot = node.parentId == INVALID_ENTITY_ID && node.kind == EntityKind::FILE;
    if (validRoot || (node.parentId != INVALID_ENTITY_ID && index_->Find(node.parentId) != nullptr)) {
        if (index_->Add(std::move(node))) {
            return true;
        }
    }
    return Fail("Failed to add a structure node");
}

EntityId HierarchyBuilder::EnsureGroup(EntityId parentId, EntityGroup group)
{
    const auto key = std::make_pair(parentId, group);
    const auto found = groups_.find(key);
    if (found != groups_.end()) {
        return found->second;
    }
    EntityNode node(NextId(), parentId, EntityKind::GROUP, GroupName(group));
    node.group = group;
    if (group == EntityGroup::COMPILER_GENERATED) {
        node.role = EntityRole::COMPILER_GENERATED;
    } else if (group == EntityGroup::ABC_INTERNALS) {
        node.role = EntityRole::ABC_INTERNAL;
    }
    node.origin = EntityOrigin::RECONSTRUCTED;
    node.defaultCollapsed = true;
    const auto id = node.id;
    if (!AddNode(std::move(node))) {
        return INVALID_ENTITY_ID;
    }
    groups_.emplace(key, id);
    return id;
}

void HierarchyBuilder::AddBacking(EntityNode *node, PhysicalEntityKind kind, uint64_t offset, uint64_t size,
                                  const std::string &name)
{
    const auto range = RangeFor(offset, size, program_.fileSize);
    if (!range.has_value()) {
        // Context is required: one node can register several out-of-range
        // backing entries (e.g. a reconstructed class adds its constructor
        // METHOD, LITERAL_ARRAY and annotations), and a bare fixed message
        // would repeat verbatim once per entry with nothing to tell them
        // apart. Name the entity and the offending physical range.
        std::ostringstream message;
        message << "Physical range is outside the ABC file: " << PhysicalEntityKindName(kind);
        if (!name.empty()) {
            message << " '" << name << "'";
        }
        message << " at 0x" << std::hex << offset << '+' << std::dec << size;
        node->diagnostics.push_back({DiagnosticSeverity::WARNING, message.str(), std::nullopt});
        return;
    }
    if (!node->primaryRange.has_value()) {
        node->primaryRange = range;
    }
    AddReference(node, kind, *range, name);
}

void HierarchyBuilder::AddReference(EntityNode *node, PhysicalEntityKind kind, FileRange range, const std::string &name)
{
    node->backing.push_back({kind, range, name});
    Consume(kind, range.offset);
}

void HierarchyBuilder::AddCodeBacking(EntityNode *node, const PhysicalMethod &method)
{
    if (method.codeRange.has_value()) {
        AddReference(node, PhysicalEntityKind::CODE, *method.codeRange, method.name);
    }
}

bool HierarchyBuilder::AddAnnotations(EntityId parentId, const std::vector<PhysicalAnnotation> &annotations)
{
    for (const auto &annotation : annotations) {
        if (AnnotationAnalyzer::IsCompilerOwned(annotation)) {
            auto *owner = Mutable(parentId);
            if (owner == nullptr) {
                return Fail("Missing owner for compiler annotation " + annotation.name);
            }
            AddBacking(owner, PhysicalEntityKind::ANNOTATION, annotation.offset, annotation.size, annotation.name);
            continue;
        }
        EntityNode node(NextId(), parentId, EntityKind::ANNOTATION, annotation.name);
        node.internalName = AnnotationAnalyzer::Describe(annotation);
        AddBacking(&node, PhysicalEntityKind::ANNOTATION, annotation.offset, annotation.size, annotation.name);
        if (!AddNode(std::move(node))) {
            return false;
        }
    }
    return true;
}

bool HierarchyBuilder::IsConsumed(PhysicalEntityKind kind, uint64_t offset) const
{
    return consumed_.find({kind, offset}) != consumed_.end();
}

EntityNode *HierarchyBuilder::Mutable(EntityId id)
{
    return index_->FindMutable(id);
}

EntityId HierarchyBuilder::NextId()
{
    return nextId_++;
}

const char *HierarchyBuilder::GroupName(EntityGroup group)
{
    switch (group) {
        case EntityGroup::IMPORTS:
            return "Imports";
        case EntityGroup::EXPORTS:
            return "Exports";
        case EntityGroup::COMPILER_GENERATED:
            return "Compiler Generated";
        case EntityGroup::ABC_INTERNALS:
            return "ABC Internals";
        case EntityGroup::NONE:
            return "Group";
    }
    return "Group";
}

bool HierarchyBuilder::Fail(std::string message)
{
    error_ = std::move(message);
    return false;
}

void HierarchyBuilder::Consume(PhysicalEntityKind kind, uint64_t offset)
{
    consumed_.emplace(kind, offset);
}

}  // namespace panda::bytecode_explorer
