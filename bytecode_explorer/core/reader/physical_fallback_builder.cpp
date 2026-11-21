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

#include "core/reader/physical_fallback_builder.h"

#include <algorithm>
#include <map>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

constexpr size_t LITERAL_BATCH_SIZE = 256U;
constexpr size_t METHOD_BATCH_SIZE = 256U;
constexpr const char *DEFAULT_METHOD_DIAGNOSTIC = "Method could not be classified as a source-level declaration";

}  // namespace

PhysicalFallbackBuilder::PhysicalFallbackBuilder(const PhysicalProgram &program, HierarchyBuilder *hierarchy)
    : program_(program), hierarchy_(hierarchy)
{
}

bool PhysicalFallbackBuilder::Build(const std::vector<PhysicalFallbackContext> &contexts)
{
    if (hierarchy_ == nullptr) {
        return false;
    }
    for (const auto &context : contexts) {
        if (context.record == nullptr || context.parentId == INVALID_ENTITY_ID) {
            return false;
        }
        const auto &record = *context.record;
        const auto groupId = hierarchy_->EnsureGroup(context.parentId, EntityGroup::ABC_INTERNALS);
        if (!record.isSynthetic && !hierarchy_->IsConsumed(PhysicalEntityKind::RECORD, record.offset) &&
            !AddPhysical(groupId, PhysicalEntityKind::RECORD, record.offset, record.size, record.name)) {
            return false;
        }
        if (!AddFields(record, groupId) || !AddMethods(record, context, groupId)) {
            return false;
        }
    }
    return AddLiteralArrays();
}

bool PhysicalFallbackBuilder::AddFields(const PhysicalRecord &record, EntityId groupId)
{
    std::vector<const PhysicalField *> fields;
    for (const auto &field : record.fields) {
        if (!hierarchy_->IsConsumed(PhysicalEntityKind::FIELD, field.offset)) {
            fields.push_back(&field);
        }
    }
    if (fields.empty()) {
        return true;
    }

    EntityNode node(hierarchy_->NextId(), groupId, EntityKind::ABC_ITEM,
                    "Unclassified fields (" + std::to_string(fields.size()) + ")");
    node.internalName = record.name;
    node.role = EntityRole::ABC_INTERNAL;
    for (const auto *field : fields) {
        hierarchy_->AddBacking(&node, PhysicalEntityKind::FIELD, field->offset, field->size, field->name);
    }
    node.diagnostics.push_back({DiagnosticSeverity::INFO,
                                "Unclassified physical fields retained as inspectable backing entries",
                                node.primaryRange});
    const auto nodeId = node.id;
    if (!hierarchy_->AddNode(std::move(node))) {
        return false;
    }
    for (const auto *field : fields) {
        if (!hierarchy_->AddAnnotations(nodeId, field->annotations)) {
            return false;
        }
    }
    return true;
}

std::map<std::string, std::vector<const PhysicalMethod *>> PhysicalFallbackBuilder::CollectMethodBatches(
    const PhysicalRecord &record, const PhysicalFallbackContext &context) const
{
    std::map<std::string, std::vector<const PhysicalMethod *>> batches;
    for (const auto &method : record.methods) {
        if (hierarchy_->IsConsumed(PhysicalEntityKind::METHOD, method.offset)) {
            continue;
        }
        std::string reason = DEFAULT_METHOD_DIAGNOSTIC;
        if (context.methodDiagnostics != nullptr) {
            const auto diagnostic = context.methodDiagnostics->find(method.offset);
            if (diagnostic != context.methodDiagnostics->end()) {
                reason = diagnostic->second;
            }
        }
        batches[std::move(reason)].push_back(&method);
    }
    return batches;
}

// Appends the unclassified methods of one diagnostic reason as batched ABC internal nodes.
bool PhysicalFallbackBuilder::AppendMethodBatch(const std::string &reason,
                                                const std::vector<const PhysicalMethod *> &methods, EntityId groupId)
{
    for (size_t begin = 0; begin < methods.size(); begin += METHOD_BATCH_SIZE) {
        const auto end = std::min(begin + METHOD_BATCH_SIZE, methods.size());
        EntityNode node(hierarchy_->NextId(), groupId, EntityKind::ABC_ITEM,
                        "Unclassified methods starting @" + std::to_string(methods[begin]->offset) + " (" +
                            std::to_string(end - begin) + ")");
        node.internalName = reason;
        node.role = EntityRole::ABC_INTERNAL;
        for (size_t index = begin; index < end; ++index) {
            const auto *method = methods[index];
            hierarchy_->AddBacking(&node, PhysicalEntityKind::METHOD, method->offset, method->size, method->name);
            hierarchy_->AddCodeBacking(&node, *method);
        }
        node.diagnostics.push_back({DiagnosticSeverity::WARNING, reason, node.primaryRange});
        const auto nodeId = node.id;
        if (!hierarchy_->AddNode(std::move(node))) {
            return false;
        }
        for (size_t index = begin; index < end; ++index) {
            if (!hierarchy_->AddAnnotations(nodeId, methods[index]->annotations)) {
                return false;
            }
        }
    }
    return true;
}

bool PhysicalFallbackBuilder::AddMethods(const PhysicalRecord &record, const PhysicalFallbackContext &context,
                                         EntityId groupId)
{
    const auto batches = CollectMethodBatches(record, context);
    for (const auto &[reason, methods] : batches) {
        if (!AppendMethodBatch(reason, methods, groupId)) {
            return false;
        }
    }
    return true;
}

bool PhysicalFallbackBuilder::AddLiteralArrays()
{
    std::vector<const PhysicalLiteralArray *> literals;
    for (const auto &literal : program_.literalArrays) {
        if (!hierarchy_->IsConsumed(PhysicalEntityKind::LITERAL_ARRAY, literal.range.offset)) {
            literals.push_back(&literal);
        }
    }
    if (literals.empty()) {
        return true;
    }

    const auto groupId = hierarchy_->EnsureGroup(hierarchy_->RootId(), EntityGroup::ABC_INTERNALS);
    for (size_t begin = 0; begin < literals.size(); begin += LITERAL_BATCH_SIZE) {
        const auto end = std::min(begin + LITERAL_BATCH_SIZE, literals.size());
        const auto count = end - begin;
        EntityNode node(hierarchy_->NextId(), groupId, EntityKind::ABC_ITEM,
                        "Literal arrays starting @" + std::to_string(literals[begin]->range.offset) + " (" +
                            std::to_string(count) + ")");
        node.role = EntityRole::ABC_INTERNAL;
        for (size_t index = begin; index < end; ++index) {
            const auto *literal = literals[index];
            hierarchy_->AddBacking(&node, PhysicalEntityKind::LITERAL_ARRAY, literal->range.offset, literal->range.size,
                                   "literal array @" + std::to_string(literal->range.offset));
        }
        node.diagnostics.push_back({DiagnosticSeverity::INFO,
                                    "Unconsumed literal arrays retained as inspectable backing entries",
                                    node.primaryRange});
        if (!hierarchy_->AddNode(std::move(node))) {
            return false;
        }
    }
    return true;
}

bool PhysicalFallbackBuilder::AddPhysical(EntityId parentId, PhysicalEntityKind kind, uint64_t offset, uint64_t size,
                                          const std::string &name)
{
    EntityNode node(hierarchy_->NextId(), parentId, EntityKind::ABC_ITEM, name);
    node.internalName = name;
    node.role = EntityRole::ABC_INTERNAL;
    hierarchy_->AddBacking(&node, kind, offset, size, name);
    node.diagnostics.push_back(
        {DiagnosticSeverity::INFO, "Unconsumed physical entity retained in ABC Internals", node.primaryRange});
    return hierarchy_->AddNode(std::move(node));
}

}  // namespace panda::bytecode_explorer
