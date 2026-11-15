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

#include "core/reader/physical_reader.h"

#include "core/reader/class_definition_analyzer.h"
#include "core/reader/literal_array_reader.h"

#include "libpandafile/annotation_data_accessor.h"
#include "libpandafile/bytecode_instruction-inl.h"
#include "libpandafile/class_data_accessor-inl.h"
#include "libpandafile/code_data_accessor-inl.h"
#include "libpandafile/field_data_accessor-inl.h"
#include "libpandafile/file-inl.h"
#include "libpandafile/helpers.h"
#include "libpandafile/literal_data_accessor-inl.h"
#include "libpandafile/method_data_accessor-inl.h"
#include "libpandafile/module_data_accessor-inl.h"
#include "utils/utf.h"

#include <algorithm>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

std::string ToString(const panda_file::File::StringData &data)
{
    return utf::Mutf8AsCString(data.data);
}

std::string DescriptorToName(const uint8_t *descriptor)
{
    std::string name(utf::Mutf8AsCString(descriptor));
    if (name.size() >= 2U && name.front() == 'L' && name.back() == ';') {
        name = name.substr(1U, name.size() - 2U);
    }
    return name;
}

std::optional<FileRange> MakeRange(uint64_t offset, uint64_t size, uint64_t fileSize)
{
    FileRange range {offset, size};
    return range.IsValidFor(fileSize) ? std::make_optional(range) : std::nullopt;
}

void AddPhysicalReference(std::vector<PhysicalMetadataReference> *output, PhysicalMetadataTargetKind targetKind,
                          uint32_t targetOffset, PhysicalMetadataReferenceKind referenceKind,
                          std::optional<FileRange> sourceRange, std::optional<uint32_t> sourceMethodOffset,
                          uint32_t ordinal, std::string sourceName)
{
    if (targetOffset == 0U) {
        return;
    }
    output->push_back(
        {targetKind, referenceKind, targetOffset, sourceRange, sourceMethodOffset, ordinal, std::move(sourceName)});
}

bool IsRequiredStringReference(const PhysicalMetadataReference &reference)
{
    if (reference.targetKind != PhysicalMetadataTargetKind::STRING) {
        return false;
    }
    return reference.referenceKind == PhysicalMetadataReferenceKind::RECORD_NAME ||
           reference.referenceKind == PhysicalMetadataReferenceKind::METHOD_NAME ||
           reference.referenceKind == PhysicalMetadataReferenceKind::FIELD_NAME;
}

void BuildRequiredStringInventory(PhysicalProgram *program)
{
    for (std::size_t index = 0U; index < program->metadataReferences.size(); ++index) {
        const auto &reference = program->metadataReferences[index];
        if (IsRequiredStringReference(reference)) {
            program->requiredStringInventory.push_back({reference.targetOffset, index});
        }
    }
    const auto byOffsetThenReference = [](const auto &left, const auto &right) {
        if (left.offset != right.offset) {
            return left.offset < right.offset;
        }
        return left.representativeReferenceIndex < right.representativeReferenceIndex;
    };
    std::sort(program->requiredStringInventory.begin(), program->requiredStringInventory.end(), byOffsetThenReference);
    const auto uniqueEnd = std::unique(program->requiredStringInventory.begin(), program->requiredStringInventory.end(),
                                       [](const auto &left, const auto &right) { return left.offset == right.offset; });
    std::vector<PhysicalRequiredStringEntry> compact(program->requiredStringInventory.begin(), uniqueEnd);
    program->requiredStringInventory.swap(compact);
}

std::string AnnotationName(const panda_file::File &file, panda_file::File::EntityId annotationId)
{
    panda_file::AnnotationDataAccessor annotation(file, annotationId);
    panda_file::ClassDataAccessor annotationClass(file, annotation.GetClassId());
    auto name = DescriptorToName(annotationClass.GetDescriptor());
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        name = name.substr(dot + 1U);
    }
    constexpr std::string_view magicPrefix = "__$$ETS_ANNOTATION$$__";
    if (name.compare(0, magicPrefix.size(), magicPrefix) == 0) {
        name.erase(0, magicPrefix.size());
    }
    return name;
}

// Shared provenance for annotation element reads: the reference sink, the annotation's own range
// and the location metadata of the annotated entity.
struct AnnotationElementContext {
    std::vector<PhysicalMetadataReference> *metadataReferences {nullptr};
    std::optional<FileRange> sourceRange;
    std::optional<uint32_t> sourceMethodOffset;
    std::string sourceName;
};

// Formats one numeric scalar element into its display value.
template <typename T>
void FormatScalarAnnotationValue(const panda_file::ScalarValue &scalar, PhysicalAnnotationElement *copied)
{
    copied->displayValue = std::to_string(scalar.Get<T>());
    copied->decoded = true;
}

// Decodes one string-typed element ('C'), recording its string metadata reference.
void DecodeStringAnnotationElement(const panda_file::File &file, const panda_file::ScalarValue &scalar, size_t index,
                                   PhysicalAnnotationElement *copied, const AnnotationElementContext &source)
{
    const auto stringId = panda_file::File::EntityId(scalar.Get<uint32_t>());
    copied->displayValue = ToString(file.GetStringData(stringId));
    copied->decoded = true;
    AddPhysicalReference(source.metadataReferences, PhysicalMetadataTargetKind::STRING, stringId.GetOffset(),
                         PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE, source.sourceRange,
                         source.sourceMethodOffset, static_cast<uint32_t>(index), source.sourceName);
}

// Decodes array-typed and unknown elements: array tags summarize their element count, unknown tags
// show the raw scalar value.
void DecodeFallbackAnnotationElement(char tag, const panda_file::AnnotationDataAccessor::Elem &element,
                                     const panda_file::ScalarValue &scalar, PhysicalAnnotationElement *copied)
{
    if (tag == 'H' || (tag >= 'K' && tag <= 'Z')) {
        copied->displayValue = '[' + std::to_string(element.GetArrayValue().GetCount()) + " values]";
        copied->decoded = true;
    } else {
        copied->displayValue = "@" + std::to_string(scalar.GetValue());
    }
}

// Decodes one numeric scalar element ('1'-'9', 'A', 'B') into its display value; returns false
// for other tags.
bool DecodeNumericAnnotationElement(char tag, const panda_file::ScalarValue &scalar, PhysicalAnnotationElement *copied)
{
    switch (tag) {
        case '1':
            copied->displayValue = scalar.Get<bool>() ? "true" : "false";
            copied->decoded = true;
            return true;
        case '2':
            FormatScalarAnnotationValue<int8_t>(scalar, copied);
            return true;
        case '3':
            FormatScalarAnnotationValue<uint8_t>(scalar, copied);
            return true;
        case '4':
            FormatScalarAnnotationValue<int16_t>(scalar, copied);
            return true;
        case '5':
            FormatScalarAnnotationValue<uint16_t>(scalar, copied);
            return true;
        case '6':
            FormatScalarAnnotationValue<int32_t>(scalar, copied);
            return true;
        case '7':
            FormatScalarAnnotationValue<uint32_t>(scalar, copied);
            return true;
        case '8':
            FormatScalarAnnotationValue<int64_t>(scalar, copied);
            return true;
        case '9':
            FormatScalarAnnotationValue<uint64_t>(scalar, copied);
            return true;
        case 'A':
            FormatScalarAnnotationValue<float>(scalar, copied);
            return true;
        case 'B':
            FormatScalarAnnotationValue<double>(scalar, copied);
            return true;
        default:
            return false;
    }
}

// Reads one annotation element: its name with its metadata reference, then its decoded value.
PhysicalAnnotationElement ReadAnnotationElement(const panda_file::File &file,
                                                const panda_file::AnnotationDataAccessor &annotation, size_t index,
                                                const AnnotationElementContext &source)
{
    const auto element = annotation.GetElement(index);
    const char tag = annotation.GetTag(index).GetItem();
    PhysicalAnnotationElement copied;
    copied.name = ToString(file.GetStringData(element.GetNameId()));
    AddPhysicalReference(source.metadataReferences, PhysicalMetadataTargetKind::STRING, element.GetNameId().GetOffset(),
                         PhysicalMetadataReferenceKind::ANNOTATION_ELEMENT_NAME, source.sourceRange,
                         source.sourceMethodOffset, static_cast<uint32_t>(index), source.sourceName);
    copied.tag = tag;
    const auto scalar = element.GetScalarValue();
    if (DecodeNumericAnnotationElement(tag, scalar, &copied)) {
        return copied;
    }
    switch (tag) {
        case 'C':
            DecodeStringAnnotationElement(file, scalar, index, &copied, source);
            break;
        case '*':
            copied.displayValue = "null";
            copied.decoded = true;
            break;
        default:
            DecodeFallbackAnnotationElement(tag, element, scalar, &copied);
            break;
    }
    return copied;
}

// Shared provenance for reading one entity's annotations: the reference sink and the location
// metadata of the annotated entity.
struct AnnotationReadContext {
    std::vector<PhysicalMetadataReference> *metadataReferences {nullptr};
    std::optional<uint32_t> sourceMethodOffset;
    std::string sourceName;
};

PhysicalAnnotation ReadAnnotation(const panda_file::File &file, panda_file::File::EntityId annotationId,
                                  const AnnotationReadContext &context)
{
    panda_file::AnnotationDataAccessor annotation(file, annotationId);
    PhysicalAnnotation physical;
    physical.offset = annotationId.GetOffset();
    physical.size = annotation.GetSize();
    physical.name = AnnotationName(file, annotationId);
    const auto sourceRange = std::make_optional(FileRange {physical.offset, physical.size});
    AddPhysicalReference(context.metadataReferences, PhysicalMetadataTargetKind::STRING,
                         annotation.GetClassId().GetOffset(), PhysicalMetadataReferenceKind::ANNOTATION_NAME,
                         sourceRange, context.sourceMethodOffset, 0U, context.sourceName);
    const AnnotationElementContext elementSource {context.metadataReferences, sourceRange, context.sourceMethodOffset,
                                                  context.sourceName};
    for (size_t index = 0; index < annotation.GetCount(); ++index) {
        physical.elements.push_back(ReadAnnotationElement(file, annotation, index, elementSource));
    }
    return physical;
}

// Which annotation set of one entity to read: the runtime-applied or the declared annotations.
enum class AnnotationSet {
    RUNTIME,
    DECLARED,
};

// Reads one annotation set of one entity, runtime-applied or declared, into the annotation list.
template <class Accessor>
void ReadAnnotations(const panda_file::File &file, Accessor *accessor, AnnotationSet set,
                     std::vector<PhysicalAnnotation> *annotations, const AnnotationReadContext &context)
{
    const auto read = [&file, annotations, &context](panda_file::File::EntityId id) {
        annotations->push_back(ReadAnnotation(file, id, context));
    };
    if (set == AnnotationSet::RUNTIME) {
        accessor->EnumerateRuntimeAnnotations(read);
    } else {
        accessor->EnumerateAnnotations(read);
    }
}

std::optional<panda_file::File::EntityId> ResolveIndex(const panda_file::File &file,
                                                       panda_file::File::EntityId methodId,
                                                       const BytecodeInstruction &instruction, size_t index)
{
    if (!BytecodeInstruction::HasId(instruction.GetFormat(), index)) {
        return std::nullopt;
    }
    const auto rawIndex = instruction.GetId(index).AsRawValue();
    const auto id = file.ResolveOffsetByIndex(methodId, static_cast<panda_file::File::Index>(rawIndex));
    return id.IsValid() ? std::make_optional(id) : std::nullopt;
}

void CopyRegisters(const BytecodeInstruction &instruction, BytecodeEvidence *evidence)
{
    for (size_t index = 0;
         index < evidence->registers.size() && BytecodeInstruction::HasVReg(instruction.GetFormat(), index); ++index) {
        evidence->registers[index] = instruction.GetVReg(index);
        ++evidence->registerCount;
    }
}

std::optional<BytecodeEvidenceKind> EvidenceKind(BytecodeInstruction::Opcode opcode, bool captureLegacyFlow)
{
    using Opcode = BytecodeInstruction::Opcode;
    switch (opcode) {
        case Opcode::LDUNDEFINED:
            return captureLegacyFlow ? std::make_optional(BytecodeEvidenceKind::LOAD_UNDEFINED) : std::nullopt;
        case Opcode::LDA_V8:
            return captureLegacyFlow ? std::make_optional(BytecodeEvidenceKind::LOAD_REGISTER) : std::nullopt;
        case Opcode::LDA_STR_ID16:
            return BytecodeEvidenceKind::LOAD_STRING;
        case Opcode::LDOBJBYNAME_IMM8_ID16:
        case Opcode::LDOBJBYNAME_IMM16_ID16:
            return captureLegacyFlow ? std::make_optional(BytecodeEvidenceKind::LOAD_PROPERTY_BY_NAME) : std::nullopt;
        case Opcode::STA_V8:
            return BytecodeEvidenceKind::STORE_ACCUMULATOR;
        case Opcode::STOBJBYVALUE_IMM8_V8_V8:
        case Opcode::STOBJBYVALUE_IMM16_V8_V8:
            return BytecodeEvidenceKind::STORE_BY_VALUE;
        case Opcode::STOBJBYNAME_IMM8_ID16_V8:
        case Opcode::STOBJBYNAME_IMM16_ID16_V8:
            return BytecodeEvidenceKind::STORE_PROPERTY_BY_NAME;
        case Opcode::DEFINEPROPERTYBYNAME_IMM8_ID16_V8:
            return BytecodeEvidenceKind::DEFINE_PROPERTY_BY_NAME;
        case Opcode::DEFINEFUNC_IMM8_ID16_IMM8:
        case Opcode::DEFINEFUNC_IMM16_ID16_IMM8:
            return BytecodeEvidenceKind::DEFINE_FUNCTION;
        case Opcode::DEFINEMETHOD_IMM8_ID16_IMM8:
        case Opcode::DEFINEMETHOD_IMM16_ID16_IMM8:
            return BytecodeEvidenceKind::DEFINE_METHOD;
        case Opcode::DEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
        case Opcode::DEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
            return BytecodeEvidenceKind::DEFINE_CLASS;
        case Opcode::DEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
            return captureLegacyFlow ? std::make_optional(BytecodeEvidenceKind::DEFINE_GETTER_SETTER_BY_VALUE)
                                     : std::nullopt;
        case Opcode::CREATEARRAYWITHBUFFER_IMM8_ID16:
        case Opcode::CREATEARRAYWITHBUFFER_IMM16_ID16:
        case Opcode::CREATEOBJECTWITHBUFFER_IMM8_ID16:
        case Opcode::CREATEOBJECTWITHBUFFER_IMM16_ID16:
            return BytecodeEvidenceKind::LITERAL_ARRAY_REFERENCE;
        default:
            return std::nullopt;
    }
}

bool HasStringId(BytecodeEvidenceKind kind)
{
    return kind == BytecodeEvidenceKind::LOAD_STRING || kind == BytecodeEvidenceKind::STORE_PROPERTY_BY_NAME ||
           kind == BytecodeEvidenceKind::DEFINE_PROPERTY_BY_NAME || kind == BytecodeEvidenceKind::LOAD_PROPERTY_BY_NAME;
}

bool HasMethodId(BytecodeEvidenceKind kind)
{
    return kind == BytecodeEvidenceKind::DEFINE_FUNCTION || kind == BytecodeEvidenceKind::DEFINE_METHOD ||
           kind == BytecodeEvidenceKind::DEFINE_CLASS;
}

size_t ValidatedInstructionSize(const BytecodeInstruction &instruction, uint32_t remainingSize)
{
    if (!instruction.IsPrimaryOpcodeValid()) {
        throw panda_file::helpers::FileAccessException("Invalid bytecode primary opcode");
    }
    if (instruction.IsPrefixed() && remainingSize < 2U) {
        throw panda_file::helpers::FileAccessException("Truncated prefixed bytecode opcode");
    }
    if (!instruction.IsOpcodeValid()) {
        throw panda_file::helpers::FileAccessException("Invalid bytecode opcode");
    }
    const auto instructionSize = instruction.GetSize();
    if (instructionSize == 0U || instructionSize > remainingSize) {
        throw panda_file::helpers::FileAccessException("Truncated bytecode instruction");
    }
    return instructionSize;
}

bool ContainsAccessorDefinition(const panda_file::CodeDataAccessor &code)
{
    const uint8_t *instructions = code.GetInstructions();
    uint32_t pc = 0U;
    while (pc < code.GetCodeSize()) {
        BytecodeInstruction instruction(instructions + pc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const auto instructionSize = ValidatedInstructionSize(instruction, code.GetCodeSize() - pc);
        if (instruction.GetOpcode() == BytecodeInstruction::Opcode::DEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8) {
            return true;
        }
        pc += static_cast<uint32_t>(instructionSize);
    }
    return false;
}

// Per-method context shared by the evidence resolvers: the ABC file, the method entity, its
// name and the accumulating metadata-reference list.
struct EvidenceContext {
    const panda_file::File *file {nullptr};
    panda_file::File::EntityId methodId {};
    const std::string *methodName {nullptr};
    std::vector<PhysicalMetadataReference> *metadataReferences {nullptr};
};

// Resolves the primary ID operand of an instruction with ID evidence and records the string or
// method reference plus the resolved evidence text.
void ResolvePrimaryIdEvidence(const EvidenceContext &context, const BytecodeInstruction &instruction,
                              BytecodeEvidenceKind kind, BytecodeEvidence *evidence)
{
    const auto id = ResolveIndex(*context.file, context.methodId, instruction, 0U);
    if (!id.has_value()) {
        return;
    }
    if (HasStringId(kind)) {
        evidence->text = ToString(context.file->GetStringData(*id));
        evidence->targetOffset = id->GetOffset();
        AddPhysicalReference(context.metadataReferences, PhysicalMetadataTargetKind::STRING, id->GetOffset(),
                             PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE, evidence->range,
                             context.methodId.GetOffset(), 0U, *context.methodName);
        return;
    }
    if (HasMethodId(kind)) {
        panda_file::MethodDataAccessor target(*context.file, *id);
        evidence->text = ToString(target.GetName());
        evidence->targetOffset = id->GetOffset();
        AddPhysicalReference(context.metadataReferences, PhysicalMetadataTargetKind::METHOD, id->GetOffset(),
                             PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE, evidence->range,
                             context.methodId.GetOffset(), 0U, *context.methodName);
    }
}

// Resolves the literal-array ID operand of a class-defining or literal-referencing instruction and
// records the LITERAL_ARRAY reference on the evidence.
void ResolveLiteralEvidence(const EvidenceContext &context, const BytecodeInstruction &instruction,
                            BytecodeEvidenceKind kind, BytecodeEvidence *evidence)
{
    if (kind != BytecodeEvidenceKind::DEFINE_CLASS && kind != BytecodeEvidenceKind::LITERAL_ARRAY_REFERENCE) {
        return;
    }
    const size_t operand = kind == BytecodeEvidenceKind::DEFINE_CLASS ? 1U : 0U;
    const auto literal = ResolveIndex(*context.file, context.methodId, instruction, operand);
    if (!literal.has_value()) {
        return;
    }
    evidence->literalArrayOffset = literal->GetOffset();
    AddPhysicalReference(context.metadataReferences, PhysicalMetadataTargetKind::LITERAL_ARRAY, literal->GetOffset(),
                         PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE, evidence->range,
                         context.methodId.GetOffset(), static_cast<uint32_t>(operand), *context.methodName);
}

void ReadBytecodeEvidence(const panda_file::File &file, panda_file::File::EntityId methodId,
                          panda_file::CodeDataAccessor *code, PhysicalMethod *method,
                          std::vector<PhysicalMetadataReference> *metadataReferences)
{
    const bool captureLegacyFlow = ContainsAccessorDefinition(*code);
    const uint8_t *instructions = code->GetInstructions();
    const EvidenceContext evidenceContext {&file, methodId, &method->name, metadataReferences};
    uint32_t pc = 0;
    while (pc < code->GetCodeSize()) {
        BytecodeInstruction instruction(instructions + pc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const auto instructionSize = ValidatedInstructionSize(instruction, code->GetCodeSize() - pc);

        const auto kind = EvidenceKind(instruction.GetOpcode(), captureLegacyFlow);
        if (kind.has_value()) {
            BytecodeEvidence evidence;
            evidence.kind = *kind;
            evidence.range = {file.GetIdFromPointer(instruction.GetAddress()).GetOffset(), instructionSize};
            CopyRegisters(instruction, &evidence);
            ResolvePrimaryIdEvidence(evidenceContext, instruction, *kind, &evidence);
            ResolveLiteralEvidence(evidenceContext, instruction, *kind, &evidence);
            method->evidence.push_back(std::move(evidence));
        }
        pc += static_cast<uint32_t>(instructionSize);
    }
}

std::string ReadStringOffset(const panda_file::File &file, uint32_t offset)
{
    return offset == 0U ? std::string() : ToString(file.GetStringData(panda_file::File::EntityId(offset)));
}

void ReadModuleData(const panda_file::File &file, uint32_t offset, uint64_t fileSize, PhysicalRecord *record,
                    std::vector<PhysicalMetadataReference> *metadataReferences)
{
    panda_file::ModuleDataAccessor module(file, panda_file::File::EntityId(offset));
    std::vector<std::string> requests;
    requests.reserve(module.getRequestModules().size());
    uint32_t requestOrdinal = 0U;
    const auto sourceRange = MakeRange(offset, 0U, fileSize);
    for (const auto requestOffset : module.getRequestModules()) {
        requests.push_back(ReadStringOffset(file, requestOffset));
        AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, requestOffset,
                             PhysicalMetadataReferenceKind::MODULE_REQUEST, sourceRange, std::nullopt, requestOrdinal++,
                             record->name);
    }

    uint32_t bindingOrdinal = 0U;
    module.EnumerateModuleRecord([&file, &requests, &sourceRange, record, metadataReferences, &bindingOrdinal](
                                     panda_file::ModuleTag tag, uint32_t exportNameOffset, uint32_t moduleRequestIndex,
                                     uint32_t importNameOffset, uint32_t localNameOffset) {
        ModuleBinding binding;
        binding.kind = static_cast<ModuleBindingKind>(tag);
        binding.exportName = ReadStringOffset(file, exportNameOffset);
        binding.importName = ReadStringOffset(file, importNameOffset);
        binding.localName = ReadStringOffset(file, localNameOffset);
        binding.moduleRequestIndex = moduleRequestIndex;
        const bool requiresRequest =
            tag == panda_file::ModuleTag::REGULAR_IMPORT || tag == panda_file::ModuleTag::NAMESPACE_IMPORT ||
            tag == panda_file::ModuleTag::INDIRECT_EXPORT || tag == panda_file::ModuleTag::STAR_EXPORT;
        binding.moduleRequestValid = !requiresRequest || moduleRequestIndex < requests.size();
        if (moduleRequestIndex < requests.size()) {
            binding.moduleRequest = requests[moduleRequestIndex];
        }
        AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, importNameOffset,
                             PhysicalMetadataReferenceKind::IMPORT_NAME, sourceRange, std::nullopt, bindingOrdinal,
                             record->name);
        AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, exportNameOffset,
                             PhysicalMetadataReferenceKind::EXPORT_NAME, sourceRange, std::nullopt, bindingOrdinal,
                             record->name);
        AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, localNameOffset,
                             PhysicalMetadataReferenceKind::LOCAL_NAME, sourceRange, std::nullopt, bindingOrdinal,
                             record->name);
        ++bindingOrdinal;
        record->moduleBindings.push_back(std::move(binding));
    });
    record->moduleDataRange = MakeRange(offset, 0U, fileSize);
}

void ReadScopeNames(const panda_file::File &file, uint32_t offset, std::vector<std::string> *scopeNames,
                    std::vector<PhysicalMetadataReference> *metadataReferences, const std::string &sourceName)
{
    panda_file::LiteralDataAccessor literals(file, file.GetLiteralArraysId());
    uint32_t ordinal = 0U;
    literals.EnumerateLiteralVals(
        panda_file::File::EntityId(offset),
        [&file, scopeNames, metadataReferences, &ordinal, offset,
         &sourceName](const panda_file::LiteralDataAccessor::LiteralValue &value, panda_file::LiteralTag tag) {
            if (tag == panda_file::LiteralTag::STRING) {
                const auto stringOffset = std::get<uint32_t>(value);
                scopeNames->push_back(ToString(file.GetStringData(panda_file::File::EntityId(stringOffset))));
                AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, stringOffset,
                                     PhysicalMetadataReferenceKind::SCOPE_NAME, FileRange {offset, 0U}, std::nullopt,
                                     ordinal, sourceName);
            }
            ++ordinal;
        });
}

PhysicalMethod ReadMethod(const panda_file::File &file, panda_file::MethodDataAccessor *accessor, uint64_t fileSize,
                          std::vector<PhysicalMetadataReference> *metadataReferences)
{
    PhysicalMethod method;
    method.offset = accessor->GetMethodId().GetOffset();
    method.size = accessor->GetSize();
    method.name = ToString(accessor->GetName());
    method.isStatic = accessor->IsStatic();
    AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, accessor->GetNameId().GetOffset(),
                         PhysicalMetadataReferenceKind::METHOD_NAME, FileRange {method.offset, method.size},
                         method.offset, 0U, method.name);
    const AnnotationReadContext annotationSource {metadataReferences, method.offset, method.name};
    ReadAnnotations(file, accessor, AnnotationSet::RUNTIME, &method.annotations, annotationSource);
    ReadAnnotations(file, accessor, AnnotationSet::DECLARED, &method.annotations, annotationSource);
    const auto codeId = accessor->GetCodeId();
    if (codeId.has_value()) {
        panda_file::CodeDataAccessor code(file, codeId.value());
        method.codeRange = MakeRange(codeId->GetOffset(), code.GetSize(), fileSize);
        method.numVregs = code.GetNumVregs();
        method.numArgs = code.GetNumArgs();
        ReadBytecodeEvidence(file, accessor->GetMethodId(), &code, &method, metadataReferences);
    }
    return method;
}

PhysicalField ReadField(const panda_file::File &file, panda_file::FieldDataAccessor *accessor,
                        std::vector<PhysicalMetadataReference> *metadataReferences)
{
    PhysicalField field;
    field.offset = accessor->GetFieldId().GetOffset();
    field.size = accessor->GetSize();
    field.name = ToString(file.GetStringData(accessor->GetNameId()));
    field.isStatic = accessor->IsStatic();
    field.unsignedValue = accessor->GetValue<uint32_t>();
    AddPhysicalReference(metadataReferences, PhysicalMetadataTargetKind::STRING, accessor->GetNameId().GetOffset(),
                         PhysicalMetadataReferenceKind::FIELD_NAME, FileRange {field.offset, field.size}, std::nullopt,
                         0U, field.name);
    const AnnotationReadContext annotationSource {metadataReferences, std::nullopt, field.name};
    ReadAnnotations(file, accessor, AnnotationSet::RUNTIME, &field.annotations, annotationSource);
    ReadAnnotations(file, accessor, AnnotationSet::DECLARED, &field.annotations, annotationSource);
    return field;
}

// Returns the last record with the given name, or null when absent.
PhysicalRecord *FindRecordByName(PhysicalProgram *program, const std::string &name)
{
    const auto found = std::find_if(program->records.rbegin(), program->records.rend(),
                                    [&name](const auto &record) { return record.name == name; });
    return found == program->records.rend() ? nullptr : &*found;
}

// Indexes the scope-name literal offsets declared by the _ESScopeNamesRecord fields.
std::unordered_map<std::string, uint32_t> CollectScopeNamesOffsets(const PhysicalRecord *scopeNamesRecord)
{
    std::unordered_map<std::string, uint32_t> offsets;
    if (scopeNamesRecord != nullptr) {
        for (const auto &field : scopeNamesRecord->fields) {
            if (field.unsignedValue.has_value()) {
                offsets.emplace(field.name, *field.unsignedValue);
            }
        }
    }
    return offsets;
}

// Builds one synthetic module record from an _ESModuleRecord field, reading its scope names and
// module data when the declared offsets are valid.
PhysicalRecord BuildSyntheticModule(const panda_file::File &file, const PhysicalField &field,
                                    const std::unordered_map<std::string, uint32_t> &scopeNamesOffsets,
                                    PhysicalProgram *program)
{
    PhysicalRecord module;
    module.offset = field.offset;
    module.size = field.size;
    module.name = field.name;
    module.isArkTs = true;
    module.isModule = true;
    module.isSynthetic = true;
    const auto scopeNames = scopeNamesOffsets.find(field.name);
    if (scopeNames != scopeNamesOffsets.end() && scopeNames->second != 0U) {
        ReadScopeNames(file, scopeNames->second, &module.scopeNames, &program->metadataReferences, module.name);
        module.scopeNamesRange = MakeRange(scopeNames->second, 0U, program->fileSize);
    }
    if (field.unsignedValue.has_value() && *field.unsignedValue != 0U) {
        ReadModuleData(file, *field.unsignedValue, program->fileSize, &module, &program->metadataReferences);
    }
    return module;
}

// Marks every non-compiler-container record as owned by the one synthetic module.
void AssignSingleModuleOwner(PhysicalProgram *program, const std::string &moduleName)
{
    for (auto &record : program->records) {
        const bool compilerContainer = record.name == "_ESModuleRecord" || record.name == "_ESScopeNamesRecord" ||
                                       record.name == "_GLOBAL" || record.name.compare(0, 3U, "_ES") == 0;
        if (!compilerContainer) {
            record.ownerModuleName = moduleName;
        }
    }
}

void NormalizeNonMergeModules(const panda_file::File &file, PhysicalProgram *program)
{
    PhysicalRecord *const moduleRecord = FindRecordByName(program, "_ESModuleRecord");
    PhysicalRecord *const scopeNamesRecord = FindRecordByName(program, "_ESScopeNamesRecord");
    PhysicalRecord *const globalRecord = FindRecordByName(program, "_GLOBAL");
    if (moduleRecord == nullptr) {
        if (globalRecord != nullptr) {
            globalRecord->isArkTs = true;
            globalRecord->isScript = true;
        }
        return;
    }
    const auto scopeNamesOffsets = CollectScopeNamesOffsets(scopeNamesRecord);

    std::vector<PhysicalRecord> modules;
    modules.reserve(moduleRecord->fields.size());
    for (const auto &field : moduleRecord->fields) {
        if (!field.unsignedValue.has_value()) {
            continue;
        }
        modules.push_back(BuildSyntheticModule(file, field, scopeNamesOffsets, program));
    }

    // A non-merge ABC is emitted for one source module. Its executable methods live in the
    // physical _GLOBAL record, while _ESModuleRecord carries the logical module identity.
    // Keep ambiguous multi-module containers conservative instead of duplicating methods.
    if (modules.size() == 1U && globalRecord != nullptr) {
        modules.front().methods = std::move(globalRecord->methods);
        modules.front().classDefinitions = std::move(globalRecord->classDefinitions);
    }
    if (modules.size() == 1U) {
        AssignSingleModuleOwner(program, modules.front().name);
    }
    program->records.insert(program->records.end(), std::make_move_iterator(modules.begin()),
                            std::make_move_iterator(modules.end()));
}

bool IsTransientFlowEvidence(BytecodeEvidenceKind kind)
{
    return kind == BytecodeEvidenceKind::LOAD_UNDEFINED || kind == BytecodeEvidenceKind::LOAD_REGISTER ||
           kind == BytecodeEvidenceKind::LOAD_PROPERTY_BY_NAME ||
           kind == BytecodeEvidenceKind::DEFINE_GETTER_SETTER_BY_VALUE;
}

void DiscardTransientFlowEvidence(PhysicalRecord *record)
{
    for (auto &method : record->methods) {
        method.evidence.erase(
            std::remove_if(method.evidence.begin(), method.evidence.end(),
                           [](const auto &evidence) { return IsTransientFlowEvidence(evidence.kind); }),
            method.evidence.end());
    }
}

// Synthetic-module offsets declared by the _ESModuleRecord-style fields of one record.
struct RecordModuleOffsets {
    uint32_t scopeNamesOffset {0};
    uint32_t moduleDataOffset {0};
    bool hasScopeNames {false};
    bool hasModuleData {false};
};

// Reads one record field, tracking the synthetic-module offsets declared by _ESModuleRecord fields.
void ReadRecordField(const panda_file::File &file, panda_file::FieldDataAccessor *fieldAccessor, PhysicalRecord *record,
                     RecordModuleOffsets *moduleOffsets, std::vector<PhysicalMetadataReference> *metadataReferences)
{
    auto field = ReadField(file, fieldAccessor, metadataReferences);
    if (field.name == "scopeNames" && field.unsignedValue.has_value()) {
        moduleOffsets->hasScopeNames = true;
        moduleOffsets->scopeNamesOffset = *field.unsignedValue;
    } else if (field.name == "moduleRecordIdx" && field.unsignedValue.has_value()) {
        moduleOffsets->hasModuleData = true;
        moduleOffsets->moduleDataOffset = *field.unsignedValue;
    }
    record->fields.push_back(std::move(field));
}

// Reads one class record into the program: identity, annotations, fields, methods and the
// synthetic-module data it declares.
void ReadOneClassRecord(const panda_file::File &file, uint32_t offset, PhysicalProgram *program)
{
    panda_file::File::EntityId recordId(offset);
    panda_file::ClassDataAccessor accessor(file, recordId);
    PhysicalRecord record;
    record.offset = offset;
    record.name = DescriptorToName(accessor.GetDescriptor());
    const auto language = accessor.GetSourceLang();
    record.isArkTs = language.has_value() && language.value() == panda_file::SourceLang::ARKTS;
    record.isInterface = accessor.IsInterface();
    record.isAnnotationInterface = accessor.IsAnnotation();
    const AnnotationReadContext annotationSource {&program->metadataReferences, std::nullopt, record.name};
    ReadAnnotations(file, &accessor, AnnotationSet::RUNTIME, &record.annotations, annotationSource);
    ReadAnnotations(file, &accessor, AnnotationSet::DECLARED, &record.annotations, annotationSource);

    RecordModuleOffsets moduleOffsets;
    accessor.EnumerateFields([&file, &record, program, &moduleOffsets](panda_file::FieldDataAccessor &fieldAccessor) {
        ReadRecordField(file, &fieldAccessor, &record, &moduleOffsets, &program->metadataReferences);
    });
    accessor.EnumerateMethods([&file, &record, program](panda_file::MethodDataAccessor &methodAccessor) {
        record.methods.push_back(ReadMethod(file, &methodAccessor, program->fileSize, &program->metadataReferences));
    });
    ClassDefinitionAnalyzer::AnalyzeInitializers(&record);
    DiscardTransientFlowEvidence(&record);
    record.size = accessor.GetSize();
    AddPhysicalReference(&program->metadataReferences, PhysicalMetadataTargetKind::STRING, offset,
                         PhysicalMetadataReferenceKind::RECORD_NAME, FileRange {record.offset, record.size},
                         std::nullopt, 0U, record.name);
    record.isModule = record.isArkTs && moduleOffsets.hasModuleData;
    if (record.isModule && moduleOffsets.scopeNamesOffset != 0U) {
        ReadScopeNames(file, moduleOffsets.scopeNamesOffset, &record.scopeNames, &program->metadataReferences,
                       record.name);
        record.scopeNamesRange = MakeRange(moduleOffsets.scopeNamesOffset, 0U, program->fileSize);
    }
    if (record.isModule && moduleOffsets.moduleDataOffset != 0U) {
        ReadModuleData(file, moduleOffsets.moduleDataOffset, program->fileSize, &record, &program->metadataReferences);
    }
    program->records.push_back(std::move(record));
}

// Literal-array offsets to read: all known arrays, the subset that is walkable, and the module-data
// offsets to skip.
struct LiteralOffsetInventory {
    std::set<uint32_t> literalOffsets;
    std::set<uint32_t> traversableLiteralOffsets;
    std::set<uint32_t> moduleDataOffsets;
};

// Adds the literal-array offsets referenced by one record's method evidence; all of them are
// traversable.
void CollectEvidenceLiteralOffsets(const PhysicalRecord &record, LiteralOffsetInventory *inventory)
{
    for (const auto &method : record.methods) {
        for (const auto &evidence : method.evidence) {
            if (evidence.literalArrayOffset.has_value()) {
                inventory->literalOffsets.emplace(*evidence.literalArrayOffset);
                inventory->traversableLiteralOffsets.emplace(*evidence.literalArrayOffset);
            }
        }
    }
}

// Collects the literal-array offsets to read: the header index, module scope-name arrays and
// evidence references.
void CollectLiteralOffsets(const panda_file::File &file, const PhysicalProgram &program,
                           LiteralOffsetInventory *inventory)
{
    if (panda_file::ContainsLiteralArrayInHeader(file.GetHeader()->version)) {
        panda_file::LiteralDataAccessor indexedLiterals(file, file.GetLiteralArraysId());
        for (size_t index = 0; index < indexedLiterals.GetLiteralNum(); ++index) {
            inventory->literalOffsets.emplace(indexedLiterals.GetLiteralArrayId(index).GetOffset());
        }
    }
    for (const auto &record : program.records) {
        if (record.moduleDataRange.has_value()) {
            inventory->moduleDataOffsets.emplace(static_cast<uint32_t>(record.moduleDataRange->offset));
        }
        if (record.scopeNamesRange.has_value()) {
            const auto offset = static_cast<uint32_t>(record.scopeNamesRange->offset);
            inventory->literalOffsets.emplace(offset);
            inventory->traversableLiteralOffsets.emplace(offset);
        }
        CollectEvidenceLiteralOffsets(record, inventory);
    }
}

}  // namespace

PhysicalReader::ReadResult PhysicalReader::Read(const panda_file::File &file)
{
    try {
        PhysicalProgram program;
        program.fileSize = file.GetHeader()->file_size;
        for (const uint32_t offset : file.GetClasses()) {
            if (!file.IsExternal(panda_file::File::EntityId(offset))) {
                ReadOneClassRecord(file, offset, &program);
            }
        }
        NormalizeNonMergeModules(file, &program);
        LiteralOffsetInventory inventory;
        CollectLiteralOffsets(file, program, &inventory);
        program.literalArrays =
            LiteralArrayReader::Read(file, std::move(inventory.literalOffsets),
                                     std::move(inventory.traversableLiteralOffsets), inventory.moduleDataOffsets);
        std::sort(program.literalArrays.begin(), program.literalArrays.end(),
                  [](const auto &left, const auto &right) { return left.range.offset < right.range.offset; });
        std::stable_sort(program.records.begin(), program.records.end(),
                         [](const auto &left, const auto &right) { return left.offset < right.offset; });
        ClassDefinitionAnalyzer::AttachLiteralMethods(&program);
        BuildRequiredStringInventory(&program);
        return ReadResult::Success(std::move(program));
    } catch (const std::exception &exception) {
        return ReadResult::Failure(
            Status(ErrorCode::BACKEND_ERROR, "Failed to read physical ABC entities: " + std::string(exception.what())));
    }
}

}  // namespace panda::bytecode_explorer
