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

#ifndef BYTECODE_EXPLORER_CORE_MODEL_ENTITY_H
#define BYTECODE_EXPLORER_CORE_MODEL_ENTITY_H

#include "core/model/file_range.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {

using EntityId = uint64_t;
constexpr EntityId INVALID_ENTITY_ID = 0;

enum class EntityKind : uint8_t {
    FILE,
    MODULE,
    SCRIPT,
    GROUP,
    NAMESPACE,
    CLASS,
    INTERFACE,
    ENUM,
    ANNOTATION_INTERFACE,
    FUNCTION,
    METHOD,
    FIELD,
    ENUM_MEMBER,
    IMPORT,
    EXPORT,
    ANNOTATION,
    ABC_ITEM,
    COUNT,
};

enum class EntityRole : uint8_t {
    NORMAL,
    CONSTRUCTOR,
    INSTANCE_METHOD,
    STATIC_METHOD,
    GETTER,
    SETTER,
    MODULE_INITIALIZER,
    NAMESPACE_INITIALIZER,
    ENUM_INITIALIZER,
    COMPILER_GENERATED,
    ABC_INTERNAL,
};

enum class EntityOrigin : uint8_t {
    PHYSICAL,
    RECONSTRUCTED,
    INFERRED,
};

enum class EntityGroup : uint8_t {
    NONE,
    IMPORTS,
    EXPORTS,
    COMPILER_GENERATED,
    ABC_INTERNALS,
};

enum class DiagnosticSeverity : uint8_t {
    INFO,
    WARNING,
    ERROR,
};

enum class PhysicalEntityKind : uint8_t {
    FILE,
    RECORD,
    METHOD,
    FIELD,
    LITERAL_ARRAY,
    CODE,
    ANNOTATION,
    MODULE_DATA,
};

struct PhysicalReference {
    PhysicalEntityKind kind {PhysicalEntityKind::RECORD};
    FileRange range;
    std::string name;
};

struct EntityDiagnostic {
    DiagnosticSeverity severity {DiagnosticSeverity::INFO};
    std::string message;
    std::optional<FileRange> range;
};

struct EntityNode {
    EntityNode() = default;
    EntityNode(EntityId nodeId, EntityId nodeParentId, EntityKind nodeKind, std::string nodeName)
        : id(nodeId), parentId(nodeParentId), kind(nodeKind), name(std::move(nodeName))
    {
    }

    EntityId id {INVALID_ENTITY_ID};
    EntityId parentId {INVALID_ENTITY_ID};
    EntityKind kind {EntityKind::FILE};
    EntityRole role {EntityRole::NORMAL};
    EntityGroup group {EntityGroup::NONE};
    EntityOrigin origin {EntityOrigin::PHYSICAL};
    std::string name;
    std::string displayName;
    std::string internalName;
    std::optional<FileRange> primaryRange;
    std::vector<PhysicalReference> backing;
    std::vector<EntityDiagnostic> diagnostics;
    bool isStatic {false};
    bool defaultCollapsed {false};
    std::vector<EntityId> children;

    const std::string &VisibleName() const
    {
        return displayName.empty() ? name : displayName;
    }
};

const char *EntityKindName(EntityKind kind);
const char *EntityRoleName(EntityRole role);
const char *EntityOriginName(EntityOrigin origin);
const char *PhysicalEntityKindName(PhysicalEntityKind kind);

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_MODEL_ENTITY_H
