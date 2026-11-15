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

#include "core/reader/structure_classifier.h"

#include "core/reader/callable_analyzer.h"
#include "core/reader/enum_analyzer.h"
#include "core/reader/hierarchy_builder.h"
#include "core/reader/module_binding_analyzer.h"
#include "core/reader/physical_fallback_builder.h"
#include "core/reader/property_analyzer.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

constexpr char PATH_SEPARATOR = '\x1f';
std::string PathKey(const std::vector<std::string> &parts, size_t count)
{
    std::string key;
    for (size_t index = 0; index < count; ++index) {
        if (!key.empty()) {
            key.push_back(PATH_SEPARATOR);
        }
        key += parts[index];
    }
    return key;
}

std::string PathKey(const std::vector<std::string> &parts)
{
    return PathKey(parts, parts.size());
}

const std::vector<std::string> &ParentKeys(const ParsedDynamicName &parsed)
{
    return parsed.parentKeys.size() == parsed.parents.size() ? parsed.parentKeys : parsed.parents;
}

std::string RecordDisplayName(const std::string &recordName, const std::string &moduleName)
{
    const std::string prefix = moduleName + ".";
    if (recordName.compare(0, prefix.size(), prefix) == 0) {
        return recordName.substr(prefix.size());
    }
    const auto dot = recordName.rfind('.');
    return dot == std::string::npos ? recordName : recordName.substr(dot + 1U);
}

struct ModuleContext {
    const PhysicalRecord *physical {nullptr};
    EntityId id {INVALID_ENTITY_ID};
    std::unordered_map<std::string, EntityId> paths;
    std::unordered_map<std::string, EntityKind> pathKinds;
    std::unordered_map<std::string, std::string> pathNames;
    std::vector<AnalyzedMethod> methods;
    std::unordered_map<uint32_t, std::string> unclassifiedMethods;
};

// Outcome of refreshing an already-present callable node for a method.
enum class CallableRefresh {
    NOT_FOUND,  // no compatible node exists; the caller must create a new one
    REFRESHED,  // an existing node was updated in place
    FAILED,     // annotation wiring failed; classification must stop
};

// The classified role of one callable: its semantic role, its provenance and whether the role
// was inferred from a paired accessor rather than parsed from the bytecode.
struct CallableClassification {
    EntityRole role {EntityRole::INSTANCE_METHOD};
    EntityOrigin origin {EntityOrigin::PHYSICAL};
    bool inferredAccessor {false};
};

class Builder final {
public:
    Builder(std::string path, const PhysicalProgram &program)
        : path_(std::move(path)), program_(program), hierarchy_(path_, program_)
    {
    }

    StructureClassifier::BuildResult Build()
    {
        if (!hierarchy_.Initialize() || !AddModules()) {
            return StructureClassifier::BuildResult::Failure(Status(ErrorCode::BACKEND_ERROR, ErrorMessage()));
        }
        if (modules_.empty()) {
            return StructureClassifier::BuildResult::Failure(
                Status(ErrorCode::UNSUPPORTED_FORMAT, "No dynamic ArkTS V1 modules found in: " + path_));
        }
        if (!AddRecordEntities() || !BuildModuleSemantics() || !AddUnconsumedPhysicalEntities()) {
            return StructureClassifier::BuildResult::Failure(Status(ErrorCode::BACKEND_ERROR, ErrorMessage()));
        }
        hierarchy_.Finalize();
        return StructureClassifier::BuildResult::Success(hierarchy_.Index());
    }

private:
    bool AddModules()
    {
        for (const auto &record : program_.records) {
            if (!record.isModule && !record.isScript) {
                continue;
            }
            EntityNode module(hierarchy_.NextId(), hierarchy_.RootId(),
                              record.isScript ? EntityKind::SCRIPT : EntityKind::MODULE,
                              record.isScript ? "<script>" : record.name);
            module.internalName = record.name;
            module.origin = EntityOrigin::PHYSICAL;
            hierarchy_.AddBacking(&module, record.isSynthetic ? PhysicalEntityKind::FIELD : PhysicalEntityKind::RECORD,
                                  record.offset, record.size, record.name);
            if (record.moduleDataRange.has_value()) {
                hierarchy_.AddReference(&module, PhysicalEntityKind::MODULE_DATA, *record.moduleDataRange, record.name);
            }
            if (record.scopeNamesRange.has_value()) {
                hierarchy_.AddReference(&module, PhysicalEntityKind::LITERAL_ARRAY, *record.scopeNamesRange,
                                        "scope names for " + record.name);
            }
            const auto moduleId = module.id;
            if (!hierarchy_.AddNode(std::move(module)) || !hierarchy_.AddAnnotations(moduleId, record.annotations)) {
                return false;
            }
            modules_.emplace(record.name, ModuleContext {&record, moduleId, {}, {}, {}, {}, {}});
        }
        return true;
    }

    ModuleContext *OwningModule(const std::string &recordName)
    {
        ModuleContext *best = nullptr;
        size_t bestLength = 0;
        for (auto &[name, module] : modules_) {
            const std::string prefix = name + ".";
            if (name.size() > bestLength && recordName.compare(0, prefix.size(), prefix) == 0) {
                best = &module;
                bestLength = name.size();
            }
        }
        return best;
    }

    ModuleContext *OwningModule(const PhysicalRecord &record)
    {
        if (record.ownerModuleName.has_value()) {
            const auto found = modules_.find(*record.ownerModuleName);
            if (found != modules_.end()) {
                return &found->second;
            }
        }
        return OwningModule(record.name);
    }

    bool AddRecordEntities()
    {
        for (const auto &record : program_.records) {
            if (record.isModule || record.isScript) {
                continue;
            }
            auto *module = OwningModule(record);
            if (module != nullptr && (record.isAnnotationInterface || record.isInterface)) {
                const auto kind =
                    record.isAnnotationInterface ? EntityKind::ANNOTATION_INTERFACE : EntityKind::INTERFACE;
                EntityNode entity(hierarchy_.NextId(), module->id, kind,
                                  RecordDisplayName(record.name, module->physical->name));
                entity.internalName = record.name;
                hierarchy_.AddBacking(&entity, PhysicalEntityKind::RECORD, record.offset, record.size, record.name);
                const auto entityId = entity.id;
                if (!hierarchy_.AddNode(std::move(entity)) || !AddPhysicalFields(record, entityId) ||
                    !AddPhysicalMethods(record, entityId) || !hierarchy_.AddAnnotations(entityId, record.annotations)) {
                    return false;
                }
                continue;
            }

            const bool compilerGenerated = !record.isArkTs || record.name.compare(0, 3U, "_ES") == 0;
            const auto group = compilerGenerated ? EntityGroup::COMPILER_GENERATED : EntityGroup::ABC_INTERNALS;
            const auto parentId = hierarchy_.EnsureGroup(module == nullptr ? hierarchy_.RootId() : module->id, group);
            EntityNode internal(hierarchy_.NextId(), parentId, EntityKind::ABC_ITEM, record.name);
            internal.internalName = record.name;
            internal.role = compilerGenerated ? EntityRole::COMPILER_GENERATED : EntityRole::ABC_INTERNAL;
            internal.origin = EntityOrigin::PHYSICAL;
            hierarchy_.AddBacking(&internal, PhysicalEntityKind::RECORD, record.offset, record.size, record.name);
            internal.diagnostics.push_back({DiagnosticSeverity::INFO,
                                            compilerGenerated
                                                ? "Compiler-owned record retained as physical metadata"
                                                : "Record could not be classified as a source-level declaration",
                                            internal.primaryRange});
            const auto internalId = internal.id;
            if (!hierarchy_.AddNode(std::move(internal)) ||
                !hierarchy_.AddAnnotations(internalId, record.annotations)) {
                return false;
            }
        }
        return true;
    }

    bool AddPhysicalFields(const PhysicalRecord &record, EntityId parentId)
    {
        for (const auto &field : record.fields) {
            EntityNode node(hierarchy_.NextId(), parentId, EntityKind::FIELD, field.name);
            node.internalName = record.name + "." + field.name;
            node.isStatic = field.isStatic;
            hierarchy_.AddBacking(&node, PhysicalEntityKind::FIELD, field.offset, field.size, field.name);
            const auto fieldId = node.id;
            if (!hierarchy_.AddNode(std::move(node)) || !hierarchy_.AddAnnotations(fieldId, field.annotations)) {
                return false;
            }
        }
        return true;
    }

    bool AddPhysicalMethods(const PhysicalRecord &record, EntityId parentId)
    {
        for (const auto &method : record.methods) {
            EntityNode node(hierarchy_.NextId(), parentId, EntityKind::METHOD, method.name);
            node.internalName = record.name + "." + method.name;
            node.role = method.isStatic ? EntityRole::STATIC_METHOD : EntityRole::INSTANCE_METHOD;
            node.isStatic = method.isStatic;
            hierarchy_.AddBacking(&node, PhysicalEntityKind::METHOD, method.offset, method.size, method.name);
            hierarchy_.AddCodeBacking(&node, method);
            const auto methodId = node.id;
            if (!hierarchy_.AddNode(std::move(node)) || !hierarchy_.AddAnnotations(methodId, method.annotations)) {
                return false;
            }
        }
        return true;
    }

    bool BuildModuleSemantics()
    {
        for (auto &[name, module] : modules_) {
            if (!AddModuleBindings(module) || !CollectMethods(&module) || !AddContainers(&module) ||
                !AddCallableNodes(&module) || !AddReconstructedMembers(module) || !AddModuleInternals(module)) {
                return false;
            }
        }
        return true;
    }

    bool AddModuleBindings(const ModuleContext &module)
    {
        for (const auto &binding : module.physical->moduleBindings) {
            const auto presentation = ModuleBindingAnalyzer::Analyze(binding);
            EntityNode node(hierarchy_.NextId(), hierarchy_.EnsureGroup(module.id, presentation.group),
                            presentation.kind, presentation.name);
            node.displayName = presentation.displayName;
            node.internalName = presentation.details;
            node.origin = EntityOrigin::PHYSICAL;
            if (presentation.diagnostic.has_value()) {
                node.diagnostics.push_back(
                    {DiagnosticSeverity::WARNING, *presentation.diagnostic, module.physical->moduleDataRange});
            }
            if (module.physical->moduleDataRange.has_value()) {
                node.primaryRange = module.physical->moduleDataRange;
                hierarchy_.AddReference(&node, PhysicalEntityKind::MODULE_DATA, *node.primaryRange, node.internalName);
            }
            if (!hierarchy_.AddNode(std::move(node))) {
                return false;
            }
        }
        return true;
    }

    bool CollectMethods(ModuleContext *module)
    {
        auto analysis = CallableAnalyzer::Analyze(*module->physical);
        for (const auto &unclassified : analysis.unclassified) {
            module->unclassifiedMethods.emplace(unclassified.physical->offset, unclassified.diagnostic);
        }
        for (auto &method : analysis.methods) {
            const auto &parsed = method.parsed;
            const auto &parentKeys = ParentKeys(parsed);
            for (size_t index = 0; index < parsed.parents.size(); ++index) {
                const auto path = PathKey(parentKeys, index + 1U);
                module->pathKinds[path] = parsed.parentKinds[index];
                module->pathNames[path] = parsed.parents[index];
            }
            if (parsed.kind == EntityKind::NAMESPACE || parsed.kind == EntityKind::ENUM ||
                parsed.role == EntityRole::CONSTRUCTOR) {
                auto declarationPath = parentKeys;
                if (parsed.role != EntityRole::CONSTRUCTOR) {
                    declarationPath.push_back(parsed.name);
                }
                const auto path = PathKey(declarationPath);
                module->pathKinds[path] = parsed.role == EntityRole::CONSTRUCTOR ? EntityKind::CLASS : parsed.kind;
                module->pathNames[path] = parsed.role == EntityRole::CONSTRUCTOR ? parsed.name : parsed.name;
            }
        }
        module->methods = std::move(analysis.methods);
        return true;
    }

    bool AddContainers(ModuleContext *module)
    {
        std::vector<std::pair<std::string, EntityKind>> paths(module->pathKinds.begin(), module->pathKinds.end());
        std::sort(paths.begin(), paths.end(), [](const auto &left, const auto &right) {
            const auto leftDepth = std::count(left.first.begin(), left.first.end(), PATH_SEPARATOR);
            const auto rightDepth = std::count(right.first.begin(), right.first.end(), PATH_SEPARATOR);
            return leftDepth != rightDepth ? leftDepth < rightDepth : left.first < right.first;
        });
        for (const auto &[path, kind] : paths) {
            if (!EnsureSemanticPath(module, path, kind).has_value()) {
                return false;
            }
        }

        for (const auto &method : module->methods) {
            if (method.parsed.kind != EntityKind::NAMESPACE && method.parsed.kind != EntityKind::ENUM &&
                method.parsed.role != EntityRole::CONSTRUCTOR) {
                continue;
            }
            auto declarationPath = ParentKeys(method.parsed);
            if (method.parsed.role != EntityRole::CONSTRUCTOR) {
                declarationPath.push_back(method.parsed.name);
            }
            const auto found = module->paths.find(PathKey(declarationPath));
            if (found == module->paths.end()) {
                return Fail("Missing reconstructed declaration path for " + method.physical->name);
            }
            auto *node = hierarchy_.Mutable(found->second);
            node->role = method.parsed.role == EntityRole::CONSTRUCTOR ? EntityRole::NORMAL : method.parsed.role;
            node->internalName = method.physical->name;
            hierarchy_.AddBacking(node, PhysicalEntityKind::METHOD, method.physical->offset, method.physical->size,
                                  method.physical->name);
            hierarchy_.AddCodeBacking(node, *method.physical);
            if (method.classLiteralArrayOffset.has_value()) {
                hierarchy_.AddBacking(node, PhysicalEntityKind::LITERAL_ARRAY, *method.classLiteralArrayOffset, 0U,
                                      "class literal buffer");
            }
            if (!hierarchy_.AddAnnotations(found->second, method.physical->annotations)) {
                return false;
            }
        }
        return true;
    }

    std::optional<EntityId> EnsureSemanticPath(ModuleContext *module, const std::string &path, EntityKind finalKind)
    {
        std::vector<std::string> parts;
        size_t begin = 0;
        while (begin <= path.size()) {
            const auto end = path.find(PATH_SEPARATOR, begin);
            parts.push_back(path.substr(begin, end == std::string::npos ? path.size() - begin : end - begin));
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1U;
        }
        EntityId parentId = module->id;
        for (size_t index = 0; index < parts.size(); ++index) {
            const auto currentPath = PathKey(parts, index + 1U);
            const auto existing = module->paths.find(currentPath);
            if (existing != module->paths.end()) {
                parentId = existing->second;
                continue;
            }
            const auto knownKind = module->pathKinds.find(currentPath);
            if (knownKind == module->pathKinds.end()) {
                return std::nullopt;
            }
            const auto kind = knownKind->second;
            EntityNode node(hierarchy_.NextId(), parentId, kind, parts[index]);
            node.origin = EntityOrigin::RECONSTRUCTED;
            node.diagnostics.push_back(
                {DiagnosticSeverity::INFO, "Declaration reconstructed from dynamic scope names", std::nullopt});
            const auto display = module->pathNames.find(currentPath);
            if (display != module->pathNames.end()) {
                node.name = display->second;
            }
            parentId = node.id;
            if (!hierarchy_.AddNode(std::move(node))) {
                return std::nullopt;
            }
            module->paths.emplace(currentPath, parentId);
        }
        return parentId;
    }

    // Refreshes the already-present callable node matching this method, if any. Returns
    // NOT_FOUND when no compatible node exists (the caller then creates a new one),
    // REFRESHED when the node was updated in place, and FAILED when annotation wiring failed.
    CallableRefresh RefreshCallableNode(const ModuleContext &module, const std::vector<std::string> &callablePath,
                                        const AnalyzedMethod &method, const CallableClassification &classification)
    {
        const auto existing = module.paths.find(PathKey(callablePath));
        if (existing == module.paths.end()) {
            return CallableRefresh::NOT_FOUND;
        }
        auto *node = hierarchy_.Mutable(existing->second);
        if (node == nullptr || node->kind != method.parsed.kind) {
            return CallableRefresh::NOT_FOUND;
        }
        node->internalName = method.physical->name;
        node->role = classification.role;
        node->origin = classification.origin;
        node->isStatic = method.isStatic;
        node->diagnostics.clear();
        if (classification.inferredAccessor) {
            node->diagnostics.push_back(
                {DiagnosticSeverity::INFO, "Accessor role inferred from paired method arity", std::nullopt});
        }
        hierarchy_.AddBacking(node, PhysicalEntityKind::METHOD, method.physical->offset, method.physical->size,
                              method.physical->name);
        hierarchy_.AddCodeBacking(node, *method.physical);
        if (!hierarchy_.AddAnnotations(node->id, method.physical->annotations)) {
            return CallableRefresh::FAILED;
        }
        return CallableRefresh::REFRESHED;
    }

    // Groups instance methods by parent entity and resolved name, then classifies each group into
    // accessor roles keyed by method offset.
    std::unordered_map<uint32_t, EntityRole> CollectAccessorRoles(const ModuleContext &module) const
    {
        std::map<std::pair<EntityId, std::string>, std::vector<const PhysicalMethod *>> candidates;
        for (const auto &method : module.methods) {
            if (method.parsed.role == EntityRole::INSTANCE_METHOD) {
                const auto parent = ResolveParent(module, method.parsed);
                if (parent.has_value()) {
                    candidates[{*parent, method.parsed.name}].push_back(method.physical);
                }
            }
        }
        std::unordered_map<uint32_t, EntityRole> roles;
        for (const auto &[key, methods] : candidates) {
            const auto classified = PropertyAnalyzer::ClassifyAccessorPair(methods);
            roles.insert(classified.begin(), classified.end());
        }
        return roles;
    }

    // Resolves the effective role of one method: compiler-generated for static initializers, the
    // inferred accessor role for paired accessors, and the parsed role otherwise.
    CallableClassification ResolveCallableRole(const AnalyzedMethod &method,
                                               const std::unordered_map<uint32_t, EntityRole> &accessorRoles) const
    {
        CallableClassification classification;
        classification.role = method.parsed.role;
        if (method.parsed.name == "static_initializer") {
            classification.role = EntityRole::COMPILER_GENERATED;
        } else if (const auto accessor = accessorRoles.find(method.physical->offset); accessor != accessorRoles.end()) {
            classification.role = accessor->second;
            classification.origin = EntityOrigin::INFERRED;
            classification.inferredAccessor = true;
        }
        return classification;
    }

    // Appends one new callable node under the given parent, wiring its code backing and annotations.
    bool AppendCallableNode(const AnalyzedMethod &method, EntityId parentId,
                            const CallableClassification &classification)
    {
        EntityNode node(hierarchy_.NextId(), parentId, method.parsed.kind, method.parsed.name);
        node.internalName = method.physical->name;
        node.role = classification.role;
        node.origin = classification.origin;
        node.isStatic = method.isStatic;
        if (classification.inferredAccessor) {
            node.diagnostics.push_back(
                {DiagnosticSeverity::INFO, "Accessor role inferred from paired method arity", std::nullopt});
        }
        hierarchy_.AddBacking(&node, PhysicalEntityKind::METHOD, method.physical->offset, method.physical->size,
                              method.physical->name);
        hierarchy_.AddCodeBacking(&node, *method.physical);
        const auto callableId = node.id;
        if (!hierarchy_.AddNode(std::move(node))) {
            return false;
        }
        if (method.parsed.role != EntityRole::CONSTRUCTOR &&
            !hierarchy_.AddAnnotations(callableId, method.physical->annotations)) {
            return false;
        }
        return true;
    }

    bool AddCallableNodes(ModuleContext *module)
    {
        const auto accessorRoles = CollectAccessorRoles(*module);
        for (const auto &method : module->methods) {
            if (method.parsed.kind == EntityKind::NAMESPACE || method.parsed.kind == EntityKind::ENUM) {
                continue;
            }
            auto parentId = ResolveParent(*module, method.parsed);
            if (!parentId.has_value()) {
                module->unclassifiedMethods.emplace(method.physical->offset,
                                                    "Owning semantic scope could not be reconstructed");
                continue;
            }
            if (method.parsed.role == EntityRole::MODULE_INITIALIZER || method.parsed.name == "static_initializer") {
                parentId = hierarchy_.EnsureGroup(*parentId, EntityGroup::COMPILER_GENERATED);
            }
            const auto classification = ResolveCallableRole(method, accessorRoles);
            if (method.parsed.role != EntityRole::CONSTRUCTOR && method.parsed.name != "static_initializer") {
                auto callablePath = ParentKeys(method.parsed);
                callablePath.push_back(method.parsed.name);
                const auto refreshed = RefreshCallableNode(*module, callablePath, method, classification);
                if (refreshed == CallableRefresh::FAILED) {
                    return false;
                }
                if (refreshed == CallableRefresh::REFRESHED) {
                    continue;
                }
            }
            if (!AppendCallableNode(method, *parentId, classification)) {
                return false;
            }
        }
        return true;
    }

    std::optional<EntityId> ResolveParent(const ModuleContext &module, const ParsedDynamicName &parsed) const
    {
        const auto &parents = ParentKeys(parsed);
        if (parents.empty()) {
            return module.id;
        }
        const auto found = module.paths.find(PathKey(parents));
        return found == module.paths.end() ? std::nullopt : std::make_optional(found->second);
    }

    bool AddReconstructedMembers(const ModuleContext &module)
    {
        for (const auto &method : module.methods) {
            if (method.parsed.kind == EntityKind::ENUM) {
                auto path = ParentKeys(method.parsed);
                path.push_back(method.parsed.name);
                const auto owner = module.paths.find(PathKey(path));
                if (owner != module.paths.end() && !AddEnumMembers(owner->second, *method.physical)) {
                    return false;
                }
            }
            if (method.parsed.kind == EntityKind::NAMESPACE) {
                auto path = ParentKeys(method.parsed);
                path.push_back(method.parsed.name);
                const auto owner = module.paths.find(PathKey(path));
                if (owner != module.paths.end() && !AddStoredFields(owner->second, *method.physical, 3U, false)) {
                    return false;
                }
            }
            if (method.parsed.role == EntityRole::CONSTRUCTOR) {
                const auto owner = ResolveParent(module, method.parsed);
                if (owner.has_value() && !AddStoredFields(*owner, *method.physical, 2U, false)) {
                    return false;
                }
            }
            if (method.parsed.name == "static_initializer") {
                const auto owner = ResolveParent(module, method.parsed);
                if (owner.has_value() && !AddStoredFields(*owner, *method.physical, 2U, true)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool AddEnumMembers(EntityId enumId, const PhysicalMethod &method)
    {
        for (const auto &evidence : EnumAnalyzer::RecoverMembers(method)) {
            EntityNode member(hierarchy_.NextId(), enumId, EntityKind::ENUM_MEMBER, evidence.name);
            member.origin = EntityOrigin::RECONSTRUCTED;
            member.primaryRange = evidence.range;
            member.backing.push_back({PhysicalEntityKind::CODE, evidence.range, method.name});
            if (!hierarchy_.AddNode(std::move(member))) {
                return false;
            }
        }
        return true;
    }

    bool AddStoredFields(EntityId parentId, const PhysicalMethod &method, uint32_t receiverArgumentIndex, bool isStatic)
    {
        std::set<std::string> names;
        const auto *parent = hierarchy_.Index()->Find(parentId);
        if (parent != nullptr) {
            for (const auto childId : parent->children) {
                const auto *child = hierarchy_.Index()->Find(childId);
                if (child != nullptr) {
                    names.emplace(child->name);
                }
            }
        }
        for (const auto &property : PropertyAnalyzer::FindStoredProperties(method, receiverArgumentIndex, isStatic)) {
            if (!names.emplace(property.name).second) {
                continue;
            }
            EntityNode field(hierarchy_.NextId(), parentId, EntityKind::FIELD, property.name);
            field.origin = EntityOrigin::RECONSTRUCTED;
            field.isStatic = isStatic;
            field.primaryRange = property.range;
            field.backing.push_back({PhysicalEntityKind::CODE, property.range, method.name});
            field.diagnostics.push_back(
                {DiagnosticSeverity::INFO, "Property reconstructed from a receiver bytecode store", property.range});
            if (!hierarchy_.AddNode(std::move(field))) {
                return false;
            }
        }
        return true;
    }

    bool AddModuleInternals(const ModuleContext &module)
    {
        const auto groupId = hierarchy_.EnsureGroup(module.id, EntityGroup::ABC_INTERNALS);
        for (const auto &field : module.physical->fields) {
            EntityNode node(hierarchy_.NextId(), groupId, EntityKind::FIELD, field.name);
            node.internalName = module.physical->name + "." + field.name;
            node.role = EntityRole::ABC_INTERNAL;
            node.isStatic = field.isStatic;
            hierarchy_.AddBacking(&node, PhysicalEntityKind::FIELD, field.offset, field.size, field.name);
            node.diagnostics.push_back({DiagnosticSeverity::INFO,
                                        "Physical module metadata retained outside the semantic tree",
                                        node.primaryRange});
            if (!hierarchy_.AddNode(std::move(node))) {
                return false;
            }
        }
        return true;
    }

    bool AddUnconsumedPhysicalEntities()
    {
        std::vector<PhysicalFallbackContext> contexts;
        contexts.reserve(program_.records.size());
        for (const auto &record : program_.records) {
            EntityId parentId = hierarchy_.RootId();
            ModuleContext *module = nullptr;
            if (record.isModule || record.isScript) {
                const auto found = modules_.find(record.name);
                if (found != modules_.end()) {
                    parentId = found->second.id;
                    module = &found->second;
                }
            } else {
                module = OwningModule(record);
                if (module != nullptr) {
                    parentId = module->id;
                }
            }
            contexts.push_back({&record, parentId, module == nullptr ? nullptr : &module->unclassifiedMethods});
        }
        return PhysicalFallbackBuilder(program_, &hierarchy_).Build(contexts);
    }

    bool Fail(std::string message)
    {
        error_ = std::move(message);
        return false;
    }

    const std::string &ErrorMessage() const
    {
        return error_.empty() ? hierarchy_.Error() : error_;
    }

    std::string path_;
    const PhysicalProgram &program_;
    HierarchyBuilder hierarchy_;
    std::map<std::string, ModuleContext> modules_;
    std::string error_;
};

}  // namespace

StructureClassifier::BuildResult StructureClassifier::Build(const std::string &path, const PhysicalProgram &program)
{
    return Builder(path, program).Build();
}

}  // namespace panda::bytecode_explorer
