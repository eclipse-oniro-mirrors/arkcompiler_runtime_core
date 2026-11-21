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

#include "core/reader/class_definition_analyzer.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace panda::bytecode_explorer {
namespace {

enum class TrackedValueKind : uint8_t {
    STRING,
    METHOD,
    CLASS,
    PROTOTYPE,
};

struct TrackedValue {
    TrackedValueKind kind {TrackedValueKind::STRING};
    uint32_t offset {0};
    std::string text;
};

PhysicalClassDefinition *FindOrAdd(PhysicalRecord *record, uint32_t constructorOffset)
{
    const auto found = std::find_if(
        record->classDefinitions.begin(), record->classDefinitions.end(),
        [constructorOffset](const auto &definition) { return definition.constructorOffset == constructorOffset; });
    if (found != record->classDefinitions.end()) {
        return &*found;
    }
    record->classDefinitions.push_back({constructorOffset, std::nullopt, {}});
    return &record->classDefinitions.back();
}

void AddMember(PhysicalClassDefinition *definition, uint32_t methodOffset, const std::string &name,
               PhysicalLiteralMethodKind kind, bool isStatic)
{
    const auto duplicate = std::any_of(definition->members.begin(), definition->members.end(),
                                       [methodOffset](const auto &member) { return member.offset == methodOffset; });
    if (!duplicate) {
        definition->members.push_back({methodOffset, name, kind, isStatic});
    }
}

// Register state of one method's accumulator-tracking walk over bytecode evidence.
struct RegisterState {
    std::unordered_map<uint16_t, TrackedValue> registers;
    std::optional<TrackedValue> accumulator;
};

// Shared operands of one accessor registration: the register map with its evidence line, the
// target definition, the member name and whether the owner is a class.
struct AccessorContext {
    const std::unordered_map<uint16_t, TrackedValue> *registers {nullptr};
    const BytecodeEvidence *evidence {nullptr};
    PhysicalClassDefinition *definition {nullptr};
    const TrackedValue *name {nullptr};
    bool ownerIsClass {false};
};

// Adds the accessor held in the given register as a class member, when it is a tracked method.
void AddAccessorMember(const AccessorContext &context, size_t registerIndex, PhysicalLiteralMethodKind kind)
{
    const auto accessor = context.registers->find(context.evidence->registers[registerIndex]);
    if (accessor != context.registers->end() && accessor->second.kind == TrackedValueKind::METHOD) {
        AddMember(context.definition, accessor->second.offset, context.name->text, kind, context.ownerIsClass);
    }
}

// Applies one DEFINE_GETTER_SETTER_BY_VALUE evidence line, attaching the tracked getter and setter
// methods to the class that owns them.
void ApplyDefineGetterSetter(const BytecodeEvidence &evidence, RegisterState *state, PhysicalRecord *record)
{
    if (evidence.registerCount != evidence.registers.size()) {
        return;
    }
    const auto owner = state->registers.find(evidence.registers[0]);
    const auto name = state->registers.find(evidence.registers[1]);
    if (owner == state->registers.end() || name == state->registers.end() ||
        (owner->second.kind != TrackedValueKind::CLASS && owner->second.kind != TrackedValueKind::PROTOTYPE) ||
        name->second.kind != TrackedValueKind::STRING) {
        return;
    }
    auto *definition = FindOrAdd(record, owner->second.offset);
    const bool ownerIsClass = owner->second.kind == TrackedValueKind::CLASS;
    const AccessorContext accessorContext {&state->registers, &evidence, definition, &name->second, ownerIsClass};
    AddAccessorMember(accessorContext, 2U, PhysicalLiteralMethodKind::GETTER);
    AddAccessorMember(accessorContext, 3U, PhysicalLiteralMethodKind::SETTER);
}

// Applies one DEFINE_CLASS evidence line, registering the class and tracking it in the accumulator.
void ApplyDefineClass(const BytecodeEvidence &evidence, RegisterState *state, PhysicalRecord *record)
{
    if (!evidence.targetOffset.has_value()) {
        state->accumulator.reset();
        return;
    }
    auto *definition = FindOrAdd(record, *evidence.targetOffset);
    definition->literalArrayOffset = evidence.literalArrayOffset;
    state->accumulator = TrackedValue {TrackedValueKind::CLASS, *evidence.targetOffset, {}};
}

// Stores or clears the accumulator into its target register, mirroring the STA opcode for 8-bit register operands.
void ApplyStoreAccumulator(const BytecodeEvidence &evidence, RegisterState *state)
{
    if (evidence.registerCount != 0U && state->accumulator.has_value()) {
        state->registers[evidence.registers[0]] = *state->accumulator;
    } else if (evidence.registerCount != 0U) {
        state->registers.erase(evidence.registers[0]);
    }
}

// Feeds one evidence line into the register-tracking walk.
void ProcessEvidence(const BytecodeEvidence &evidence, RegisterState *state, PhysicalRecord *record)
{
    switch (evidence.kind) {
        case BytecodeEvidenceKind::LOAD_UNDEFINED:
            state->accumulator.reset();
            break;
        case BytecodeEvidenceKind::LOAD_REGISTER: {
            const auto value = state->registers.find(evidence.registers[0]);
            state->accumulator = value == state->registers.end() ? std::nullopt : std::make_optional(value->second);
            break;
        }
        case BytecodeEvidenceKind::LOAD_STRING:
            state->accumulator = TrackedValue {TrackedValueKind::STRING, 0U, evidence.text};
            break;
        case BytecodeEvidenceKind::LOAD_PROPERTY_BY_NAME:
            if (evidence.text == "prototype" && state->accumulator.has_value() &&
                state->accumulator->kind == TrackedValueKind::CLASS) {
                state->accumulator->kind = TrackedValueKind::PROTOTYPE;
            } else {
                state->accumulator.reset();
            }
            break;
        case BytecodeEvidenceKind::DEFINE_METHOD:
            state->accumulator =
                evidence.targetOffset.has_value()
                    ? std::make_optional(TrackedValue {TrackedValueKind::METHOD, *evidence.targetOffset, {}})
                    : std::nullopt;
            break;
        case BytecodeEvidenceKind::DEFINE_CLASS:
            ApplyDefineClass(evidence, state, record);
            break;
        case BytecodeEvidenceKind::STORE_ACCUMULATOR:
            ApplyStoreAccumulator(evidence, state);
            break;
        case BytecodeEvidenceKind::DEFINE_GETTER_SETTER_BY_VALUE:
            ApplyDefineGetterSetter(evidence, state, record);
            break;
        default:
            break;
    }
}

void AnalyzeInitializerEvidence(PhysicalRecord *record)
{
    for (const auto &method : record->methods) {
        RegisterState state;
        for (const auto &evidence : method.evidence) {
            ProcessEvidence(evidence, &state, record);
        }
    }
}

}  // namespace

void ClassDefinitionAnalyzer::AnalyzeInitializers(PhysicalRecord *record)
{
    AnalyzeInitializerEvidence(record);
}

void ClassDefinitionAnalyzer::AttachLiteralMethods(PhysicalProgram *program)
{
    std::unordered_map<uint32_t, const PhysicalLiteralArray *> literals;
    for (const auto &literal : program->literalArrays) {
        literals.emplace(static_cast<uint32_t>(literal.range.offset), &literal);
    }
    for (auto &record : program->records) {
        for (auto &definition : record.classDefinitions) {
            if (!definition.literalArrayOffset.has_value()) {
                continue;
            }
            const auto literal = literals.find(*definition.literalArrayOffset);
            if (literal == literals.end()) {
                continue;
            }
            for (const auto &method : literal->second->methods) {
                AddMember(&definition, method.offset, method.name, method.kind, method.isStatic);
            }
        }
    }
}

}  // namespace panda::bytecode_explorer
