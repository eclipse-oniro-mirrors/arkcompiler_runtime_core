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

#include "core/reader/callable_analyzer.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

struct LegacyDefinition {
    BytecodeEvidenceKind kind {BytecodeEvidenceKind::DEFINE_FUNCTION};
    const PhysicalClassDefinition *classDefinition {nullptr};
};

ParsedDynamicName LegacyFunction(const PhysicalMethod &method)
{
    ParsedDynamicName parsed;
    parsed.kind = EntityKind::FUNCTION;
    parsed.name = method.name;
    parsed.internalName = method.name;
    return parsed;
}

EntityRole MemberRole(const PhysicalClassMember &member)
{
    return member.kind == PhysicalLiteralMethodKind::GETTER   ? EntityRole::GETTER
           : member.kind == PhysicalLiteralMethodKind::SETTER ? EntityRole::SETTER
           : member.isStatic                                  ? EntityRole::STATIC_METHOD
                                                              : EntityRole::INSTANCE_METHOD;
}

ParsedDynamicName LegacyClassCallable(const PhysicalMethod &method, const std::string &className,
                                      const PhysicalClassMember *member)
{
    ParsedDynamicName parsed;
    parsed.kind = EntityKind::METHOD;
    parsed.internalName = method.name;
    parsed.parents = {className};
    parsed.parentKeys = parsed.parents;
    parsed.parentKinds = {EntityKind::CLASS};
    if (member == nullptr) {
        parsed.role = EntityRole::CONSTRUCTOR;
        parsed.name = className;
        return parsed;
    }
    parsed.name = member->name.empty() ? method.name : member->name;
    parsed.role = MemberRole(*member);
    return parsed;
}

// Index of one class member keyed by its owning method offset.
using ClassMemberIndex =
    std::unordered_map<uint32_t, std::pair<const PhysicalClassDefinition *, const PhysicalClassMember *>>;

// Indexes define-function/define-class evidence found in the func_main_0 methods.
void CollectFuncMainDefinitions(const PhysicalRecord &record,
                                std::unordered_map<uint32_t, LegacyDefinition> *definitions)
{
    for (const auto &method : record.methods) {
        if (method.name != "func_main_0" && method.name != record.name + ".func_main_0") {
            continue;
        }
        for (const auto &evidence : method.evidence) {
            if (!evidence.targetOffset.has_value() || (evidence.kind != BytecodeEvidenceKind::DEFINE_FUNCTION &&
                                                       evidence.kind != BytecodeEvidenceKind::DEFINE_CLASS)) {
                continue;
            }
            definitions->emplace(*evidence.targetOffset, LegacyDefinition {evidence.kind, nullptr});
        }
    }
}

// Indexes class definitions by constructor offset and class members by method offset.
void CollectClassDefinitions(const PhysicalRecord &record, std::unordered_map<uint32_t, LegacyDefinition> *definitions,
                             ClassMemberIndex *members)
{
    for (const auto &definition : record.classDefinitions) {
        (*definitions)[definition.constructorOffset] = {BytecodeEvidenceKind::DEFINE_CLASS, &definition};
        for (const auto &member : definition.members) {
            (*members)[member.offset] = {&definition, &member};
        }
    }
}

// Returns the method with the given offset, or null when absent.
const PhysicalMethod *FindMethod(const PhysicalRecord &record, uint32_t offset)
{
    const auto found = std::find_if(record.methods.begin(), record.methods.end(),
                                    [offset](const auto &method) { return method.offset == offset; });
    return found == record.methods.end() ? nullptr : &*found;
}

// Resolves the dynamic name of one method, preferring the class member table and falling back to
// the legacy define-function/define-class evidence.
std::optional<ParsedDynamicName> ResolveParsedName(const PhysicalRecord &record, const ClassMemberIndex &members,
                                                   const std::unordered_map<uint32_t, LegacyDefinition> &definitions,
                                                   const PhysicalMethod &method)
{
    auto parsed = DynamicNameParser::Parse(record.name, method.name, record.scopeNames);
    const auto member = members.find(method.offset);
    if (parsed.has_value() && member != members.end()) {
        parsed->name = member->second.second->name.empty() ? parsed->name : member->second.second->name;
        parsed->role = MemberRole(*member->second.second);
        return parsed;
    }
    if (parsed.has_value()) {
        return parsed;
    }
    if (member != members.end()) {
        const auto *constructor = FindMethod(record, member->second.first->constructorOffset);
        if (constructor != nullptr) {
            return LegacyClassCallable(method, constructor->name, member->second.second);
        }
        return std::nullopt;
    }
    const auto definition = definitions.find(method.offset);
    if (definition != definitions.end()) {
        return definition->second.kind == BytecodeEvidenceKind::DEFINE_CLASS
                   ? std::make_optional(LegacyClassCallable(method, method.name, nullptr))
                   : std::make_optional(LegacyFunction(method));
    }
    return std::nullopt;
}

}  // namespace

CallableAnalysis CallableAnalyzer::Analyze(const PhysicalRecord &record)
{
    std::unordered_map<uint32_t, LegacyDefinition> definitions;
    CollectFuncMainDefinitions(record, &definitions);
    ClassMemberIndex members;
    CollectClassDefinitions(record, &definitions, &members);

    CallableAnalysis result;
    result.methods.reserve(record.methods.size());
    for (const auto &method : record.methods) {
        auto parsed = ResolveParsedName(record, members, definitions, method);
        if (!parsed.has_value() || !parsed->reliable) {
            result.unclassified.push_back(
                {&method, parsed.has_value() ? parsed->diagnostic
                                             : "Method name does not match the supported dynamic ArkTS grammar"});
            continue;
        }
        std::optional<uint32_t> classLiteralArrayOffset;
        const auto definition = definitions.find(method.offset);
        if (definition != definitions.end() && definition->second.classDefinition != nullptr) {
            classLiteralArrayOffset = definition->second.classDefinition->literalArrayOffset;
        }
        const auto member = members.find(method.offset);
        const bool isStatic =
            member != members.end() ? member->second.second->isStatic : parsed->role == EntityRole::STATIC_METHOD;
        result.methods.push_back({&method, std::move(*parsed), classLiteralArrayOffset, isStatic});
    }
    return result;
}

}  // namespace panda::bytecode_explorer
