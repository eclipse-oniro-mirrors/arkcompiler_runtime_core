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

#ifndef BYTECODE_EXPLORER_CORE_MODEL_PHYSICAL_PROGRAM_H
#define BYTECODE_EXPLORER_CORE_MODEL_PHYSICAL_PROGRAM_H

#include "core/model/file_range.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {

enum class ModuleBindingKind : uint8_t {
    REGULAR_IMPORT,
    NAMESPACE_IMPORT,
    LOCAL_EXPORT,
    INDIRECT_EXPORT,
    STAR_EXPORT,
};

enum class PhysicalMetadataTargetKind : uint8_t {
    STRING,
    METHOD,
    LITERAL_ARRAY,
};

enum class PhysicalMetadataReferenceKind : uint8_t {
    RECORD_NAME,
    METHOD_NAME,
    FIELD_NAME,
    ANNOTATION_NAME,
    ANNOTATION_ELEMENT_NAME,
    ANNOTATION_STRING_VALUE,
    MODULE_REQUEST,
    IMPORT_NAME,
    EXPORT_NAME,
    LOCAL_NAME,
    SCOPE_NAME,
    LITERAL_STRING,
    LITERAL_METHOD,
    NESTED_LITERAL_ARRAY,
    INSTRUCTION_EVIDENCE,
};

struct PhysicalMetadataReference {
    PhysicalMetadataTargetKind targetKind {PhysicalMetadataTargetKind::STRING};
    PhysicalMetadataReferenceKind referenceKind {PhysicalMetadataReferenceKind::RECORD_NAME};
    uint32_t targetOffset {0U};
    std::optional<FileRange> sourceRange;
    std::optional<uint32_t> sourceMethodOffset;
    uint32_t ordinal {0U};
    std::string sourceName;
};

struct PhysicalAnnotationElement {
    std::string name;
    char tag {'?'};
    std::string displayValue;
    bool decoded {false};
};

struct PhysicalAnnotation {
    uint32_t offset {0};
    uint64_t size {0};
    std::string name;
    std::vector<PhysicalAnnotationElement> elements;
};

enum class BytecodeEvidenceKind : uint8_t {
    LOAD_UNDEFINED,
    LOAD_REGISTER,
    LOAD_STRING,
    LOAD_PROPERTY_BY_NAME,
    STORE_ACCUMULATOR,
    STORE_BY_VALUE,
    STORE_PROPERTY_BY_NAME,
    DEFINE_PROPERTY_BY_NAME,
    DEFINE_FUNCTION,
    DEFINE_METHOD,
    DEFINE_CLASS,
    DEFINE_GETTER_SETTER_BY_VALUE,
    LITERAL_ARRAY_REFERENCE,
};

struct BytecodeEvidence {
    static constexpr uint16_t INVALID_REGISTER = UINT16_MAX;

    BytecodeEvidenceKind kind {BytecodeEvidenceKind::LOAD_STRING};
    FileRange range;
    std::array<uint16_t, 4U> registers {INVALID_REGISTER, INVALID_REGISTER, INVALID_REGISTER, INVALID_REGISTER};
    uint8_t registerCount {0};
    std::string text;
    std::optional<uint32_t> targetOffset;
    std::optional<uint32_t> literalArrayOffset;
};

struct PhysicalField {
    uint32_t offset {0};
    uint64_t size {0};
    std::string name;
    bool isStatic {false};
    std::optional<uint32_t> unsignedValue;
    std::vector<PhysicalAnnotation> annotations;
};

struct PhysicalMethod {
    uint32_t offset {0};
    uint64_t size {0};
    std::string name;
    bool isStatic {false};
    uint32_t numVregs {0};
    uint32_t numArgs {0};
    std::optional<FileRange> codeRange;
    std::vector<BytecodeEvidence> evidence;
    std::vector<PhysicalAnnotation> annotations;
};

enum class PhysicalLiteralMethodKind : uint8_t {
    METHOD,
    GETTER,
    SETTER,
};

struct PhysicalLiteralMethod {
    uint32_t offset {0};
    std::string name;
    PhysicalLiteralMethodKind kind {PhysicalLiteralMethodKind::METHOD};
    bool isStatic {false};
};

using PhysicalClassMember = PhysicalLiteralMethod;

struct PhysicalClassDefinition {
    uint32_t constructorOffset {0};
    std::optional<uint32_t> literalArrayOffset;
    std::vector<PhysicalClassMember> members;
};

struct ModuleBinding {
    ModuleBindingKind kind {ModuleBindingKind::LOCAL_EXPORT};
    std::string localName;
    std::string importName;
    std::string exportName;
    std::string moduleRequest;
    uint32_t moduleRequestIndex {0};
    bool moduleRequestValid {true};
};

struct PhysicalRecord {
    uint32_t offset {0};
    uint64_t size {0};
    std::string name;
    std::optional<std::string> ownerModuleName;
    bool isArkTs {false};
    bool isModule {false};
    bool isScript {false};
    bool isSynthetic {false};
    bool isInterface {false};
    bool isAnnotationInterface {false};
    std::optional<FileRange> moduleDataRange;
    std::optional<FileRange> scopeNamesRange;
    std::vector<std::string> scopeNames;
    std::vector<ModuleBinding> moduleBindings;
    std::vector<PhysicalField> fields;
    std::vector<PhysicalMethod> methods;
    std::vector<PhysicalClassDefinition> classDefinitions;
    std::vector<PhysicalAnnotation> annotations;
};

struct PhysicalLiteralArray {
    PhysicalLiteralArray() = default;
    PhysicalLiteralArray(FileRange literalRange, uint32_t literalValueCount,
                         std::vector<uint32_t> literalNestedArrayOffsets,
                         std::vector<PhysicalLiteralMethod> literalMethods,
                         std::vector<PhysicalMetadataReference> literalMetadataReferences = {})
        : range(literalRange),
          valueCount(literalValueCount),
          nestedArrayOffsets(std::move(literalNestedArrayOffsets)),
          methods(std::move(literalMethods)),
          metadataReferences(std::move(literalMetadataReferences))
    {
    }

    FileRange range;
    uint32_t valueCount {0};
    std::vector<uint32_t> nestedArrayOffsets;
    std::vector<PhysicalLiteralMethod> methods;
    std::vector<PhysicalMetadataReference> metadataReferences;
};

struct PhysicalRequiredStringEntry {
    uint32_t offset {0U};
    std::size_t representativeReferenceIndex {0U};
};

struct PhysicalProgram {
    uint64_t fileSize {0};
    std::vector<PhysicalRecord> records;
    std::vector<PhysicalLiteralArray> literalArrays;
    std::vector<PhysicalMetadataReference> metadataReferences;
    std::vector<PhysicalRequiredStringEntry> requiredStringInventory;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_MODEL_PHYSICAL_PROGRAM_H
