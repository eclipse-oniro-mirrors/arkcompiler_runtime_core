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

#include "core/cfg/cfg_analysis.h"
#include "core/cfg/cfg_builder.h"
#include "core/document/abc_document.h"
#include "core/executable_target_resolver.h"
#include "core/function_disassembler.h"
#include "core/format/disassembly_formatter.h"
#include "core/method_index.h"
#include "core/reader/dynamic_structure_reader.h"
#include "tests/disassembly_test_utils.h"

#include "bytecode_instruction-inl.h"
#include "code_data_accessor.h"
#include "file_item_container.h"
#include "file_writer.h"
#include "literal_data_accessor.h"
#include "method_data_accessor-inl.h"
#include "modifiers.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <unistd.h>

namespace panda::bytecode_explorer::test {
namespace {

void AddCode(EntityNode *node, std::string methodName, FileRange codeRange)
{
    node->backing.push_back({PhysicalEntityKind::CODE, codeRange, std::move(methodName)});
}

const EntityNode *FindNode(const StructureIndex &index, EntityKind kind, EntityRole role, const std::string &name)
{
    const auto nodes = index.NodesInIdOrder();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [kind, role, name](const EntityNode *node) {
        return node->kind == kind && node->role == role && node->name == name;
    });
    return found == nodes.end() ? nullptr : *found;
}

uint64_t MethodTaggedValuesOffset(const panda_file::File &file, const ExecutableTarget &target)
{
    constexpr uint64_t fixedMethodHeaderSize = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
    return UlebEndOffset(file, target.methodOffset + fixedMethodHeaderSize);
}

struct RepeatedMethodAnnotationFixture {
    std::vector<uint8_t> bytes;
    uint32_t methodOffset {0U};
    uint64_t firstTagOffset {0U};
    uint64_t secondTagOffset {0U};
    uint32_t firstAnnotationOffset {0U};
    uint32_t secondAnnotationOffset {0U};
};

RepeatedMethodAnnotationFixture CreateRepeatedMethodAnnotationFixture()
{
    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LRepeatedMethodAnnotation;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *methodName = container.GetOrCreateStringItem("annotated");
    auto *returnType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::VOID);
    std::vector<panda_file::MethodParamItem> params;
    auto *prototype = container.GetOrCreateProtoItem(returnType, params);
    auto *method = classItem->AddMethod(methodName, prototype, ACC_PUBLIC | ACC_STATIC, params);

    std::vector<panda_file::AnnotationItem::Elem> elements;
    std::vector<panda_file::AnnotationItem::Tag> tags;
    auto *firstAnnotation = container.CreateItem<panda_file::AnnotationItem>(classItem, elements, tags);
    auto *secondAnnotation = container.CreateItem<panda_file::AnnotationItem>(classItem, elements, tags);
    method->AddAnnotation(firstAnnotation);
    method->AddAnnotation(secondAnnotation);

    panda_file::MemoryWriter writer;
    if (!container.Write(&writer)) {
        ADD_FAILURE() << "Failed to serialize repeated method annotations";
        return {};
    }
    auto bytes = writer.GetData();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    if (file == nullptr) {
        ADD_FAILURE() << "Failed to open serialized repeated method annotations";
        return {};
    }
    ExecutableTarget target;
    target.methodOffset = method->GetOffset();
    const auto firstTagOffset = MethodTaggedValuesOffset(*file, target);
    constexpr uint64_t taggedIdSize = 1U + sizeof(panda_file::File::EntityId);
    return {std::move(bytes),
            method->GetOffset(),
            firstTagOffset,
            firstTagOffset + taggedIdSize,
            firstAnnotation->GetOffset(),
            secondAnnotation->GetOffset()};
}

struct FieldValueAnnotationFixture {
    std::vector<uint8_t> bytes;
    uint64_t valueTagOffset {0U};
    uint64_t annotationTagOffset {0U};
};

FieldValueAnnotationFixture CreateFieldValueAnnotationFixture()
{
    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LFieldValueAnnotation;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *fieldName = container.GetOrCreateStringItem("value");
    auto *fieldType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::I32);
    auto *field = classItem->AddField(fieldName, fieldType, ACC_PUBLIC | ACC_STATIC);
    field->SetValue(container.GetOrCreateIntegerValueItem(0U));
    std::vector<panda_file::AnnotationItem::Elem> elements;
    std::vector<panda_file::AnnotationItem::Tag> tags;
    auto *annotation = container.CreateItem<panda_file::AnnotationItem>(classItem, elements, tags);
    field->AddAnnotation(annotation);

    panda_file::MemoryWriter writer;
    if (!container.Write(&writer)) {
        ADD_FAILURE() << "Failed to serialize field value and annotation";
        return {};
    }
    auto bytes = writer.GetData();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    if (file == nullptr) {
        ADD_FAILURE() << "Failed to open serialized field value and annotation";
        return {};
    }
    constexpr uint64_t fixedFieldHeaderSize = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
    const auto valueTagOffset = UlebEndOffset(*file, field->GetOffset() + fixedFieldHeaderSize);
    constexpr uint64_t zeroSlebTaggedSize = 2U;
    return {std::move(bytes), valueTagOffset, valueTagOffset + zeroSlebTaggedSize};
}

void AppendUleb32(std::vector<uint8_t> *bytes, uint32_t value)
{
    do {
        auto byte = static_cast<uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0U) {
            byte |= 0x80U;
        }
        bytes->push_back(byte);
    } while (value != 0U);
}

uint64_t CodeSizeFieldOffset(const panda_file::File &file, const ExecutableTarget &target)
{
    return GetCodeLayout(file, target).codeSizeFieldOffset;
}

uint64_t FirstJumpImmediateOffset(const panda_file::File &file, const ExecutableTarget &target)
{
    panda_file::MethodDataAccessor method(file, panda_file::File::EntityId(static_cast<uint32_t>(target.methodOffset)));
    const auto codeId = method.GetCodeId();
    EXPECT_TRUE(codeId.has_value());
    if (!codeId.has_value()) {
        return file.GetHeader()->file_size;
    }
    panda_file::CodeDataAccessor code(file, *codeId);
    const uint8_t *instructions = code.GetInstructions();
    const uint8_t *lastByte = instructions + code.GetCodeSize() - 1U;
    uint32_t offset = 0U;
    while (offset < code.GetCodeSize()) {
        const uint8_t *address = instructions + offset;
        BytecodeInstructionSafe instruction(address, instructions, lastByte);
        EXPECT_TRUE(instruction.IsPrimaryOpcodeValid());
        EXPECT_TRUE(instruction.IsOpcodeValid());
        if (instruction.IsJumpInstruction()) {
            EXPECT_EQ(instruction.GetOpcode(), BytecodeInstructionSafe::Opcode::JNEZ_IMM8);
            EXPECT_EQ(instruction.GetSize(), 2U);
            EXPECT_GT(instruction.GetImmCount(), 0U);
            if (instruction.GetOpcode() != BytecodeInstructionSafe::Opcode::JNEZ_IMM8 || instruction.GetSize() != 2U) {
                return file.GetHeader()->file_size;
            }
            return file.GetIdFromPointer(address).GetOffset() + 1U;
        }
        offset += static_cast<uint32_t>(instruction.GetSize());
    }
    return file.GetHeader()->file_size;
}

uint32_t FirstJumpInteriorBytecodeOffset(const panda_file::File &file, const ExecutableTarget &target)
{
    const auto immediateOffset = FirstJumpImmediateOffset(file, target);
    const auto layout = GetCodeLayout(file, target);
    EXPECT_GT(immediateOffset, layout.instructionsOffset);
    EXPECT_LT(immediateOffset, layout.instructionsEndOffset);
    if (immediateOffset <= layout.instructionsOffset || immediateOffset >= layout.instructionsEndOffset) {
        return layout.codeSize;
    }
    return static_cast<uint32_t>(immediateOffset - layout.instructionsOffset);
}

class FunctionDisassemblerTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto opened = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
        ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();
        document_ = opened.Value();

        auto built = DynamicStructureReader::Build(document_->Summary().path, document_->File());
        ASSERT_TRUE(built.HasValue()) << built.Error().Message();
        structure_ = built.Value();
    }

    std::vector<ExecutableTarget> Resolve(EntityKind kind, EntityRole role, const std::string &name) const
    {
        const auto *node = FindNode(*structure_, kind, role, name);
        if (node == nullptr) {
            return {};
        }
        return ExecutableTargetResolver::Resolve(*node);
    }

    std::optional<ExecutableTarget> ResolveMethodName(const std::string &methodName) const
    {
        const auto separator = methodName.find('.');
        const auto ownerName = separator == std::string::npos ? std::string {} : methodName.substr(0U, separator);
        const auto physicalName = separator == std::string::npos ? methodName : methodName.substr(separator + 1U);
        for (const auto *node : structure_->NodesInIdOrder()) {
            bool hasExpectedOwner = ownerName.empty();
            for (const EntityNode *ancestor = node; ancestor != nullptr && !hasExpectedOwner;
                 ancestor = structure_->Find(ancestor->parentId)) {
                hasExpectedOwner = ancestor->kind == EntityKind::MODULE && ancestor->name == ownerName;
            }
            if (!hasExpectedOwner) {
                continue;
            }
            const auto targets = ExecutableTargetResolver::Resolve(*node);
            const auto found =
                std::find_if(targets.begin(), targets.end(), [physicalName](const ExecutableTarget &target) {
                    return target.methodName == physicalName;
                });
            if (found != targets.end()) {
                return *found;
            }
        }
        return std::nullopt;
    }

    std::shared_ptr<const AbcDocument> document_;
    std::shared_ptr<const StructureIndex> structure_;
    FunctionDisassembler disassembler_;
};

TEST(ExecutableTargetResolverTest, FunctionAndMethodAcceptMethodBacking)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "run");
    AddMethod(&function, "entry.func_main_0", 0x30U, std::nullopt);
    auto method = MakeNode(EntityKind::METHOD, EntityRole::INSTANCE_METHOD, "move");
    AddMethod(&method, "entry.#Vehicle#>move", 0x40U, std::nullopt);

    const auto functionTargets = ExecutableTargetResolver::Resolve(function);
    const auto methodTargets = ExecutableTargetResolver::Resolve(method);

    ASSERT_EQ(functionTargets.size(), 1U);
    EXPECT_EQ(functionTargets[0].entityId, function.id);
    EXPECT_EQ(functionTargets[0].methodOffset, 0x30U);
    EXPECT_EQ(functionTargets[0].methodName, "entry.func_main_0");
    EXPECT_FALSE(functionTargets[0].codeRange.has_value());
    ASSERT_EQ(methodTargets.size(), 1U);
    EXPECT_EQ(methodTargets[0].methodOffset, 0x40U);
    EXPECT_EQ(methodTargets[0].methodName, "entry.#Vehicle#>move");
}

TEST(ExecutableTargetResolverTest, NamespaceRequiresNamespaceInitializerRole)
{
    auto initializer = MakeNode(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Geometry");
    AddMethod(&initializer, "entry.#Geometry&#Geometry", 0x50U, FileRange {0x500U, 24U});
    auto normal = MakeNode(EntityKind::NAMESPACE, EntityRole::NORMAL, "Geometry");
    AddMethod(&normal, "entry.#Geometry&#Geometry", 0x50U, FileRange {0x500U, 24U});
    auto wrongInitializer = MakeNode(EntityKind::NAMESPACE, EntityRole::ENUM_INITIALIZER, "Geometry");
    AddMethod(&wrongInitializer, "entry.#Geometry&#Geometry", 0x50U, FileRange {0x500U, 24U});

    EXPECT_EQ(ExecutableTargetResolver::Resolve(initializer).size(), 1U);
    EXPECT_TRUE(ExecutableTargetResolver::Resolve(normal).empty());
    EXPECT_TRUE(ExecutableTargetResolver::Resolve(wrongInitializer).empty());
}

TEST(ExecutableTargetResolverTest, EnumRequiresEnumInitializerRole)
{
    auto initializer = MakeNode(EntityKind::ENUM, EntityRole::ENUM_INITIALIZER, "Color");
    AddMethod(&initializer, "entry.#Color%#Color", 0x60U, FileRange {0x600U, 16U});
    auto normal = MakeNode(EntityKind::ENUM, EntityRole::NORMAL, "Color");
    AddMethod(&normal, "entry.#Color%#Color", 0x60U, FileRange {0x600U, 16U});
    auto wrongInitializer = MakeNode(EntityKind::ENUM, EntityRole::NAMESPACE_INITIALIZER, "Color");
    AddMethod(&wrongInitializer, "entry.#Color%#Color", 0x60U, FileRange {0x600U, 16U});

    EXPECT_EQ(ExecutableTargetResolver::Resolve(initializer).size(), 1U);
    EXPECT_TRUE(ExecutableTargetResolver::Resolve(normal).empty());
    EXPECT_TRUE(ExecutableTargetResolver::Resolve(wrongInitializer).empty());
}

TEST(ExecutableTargetResolverTest, AbcItemAcceptsMethodBacking)
{
    auto item = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&item, "entry.orphan", 0x70U, std::nullopt);

    const auto targets = ExecutableTargetResolver::Resolve(item);

    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets[0].methodOffset, 0x70U);
    EXPECT_EQ(targets[0].methodName, "entry.orphan");
}

TEST(ExecutableTargetResolverTest, NonExecutableKindsRejectMethodBacking)
{
    constexpr std::array nonExecutableKinds = {
        EntityKind::FILE,
        EntityKind::MODULE,
        EntityKind::SCRIPT,
        EntityKind::GROUP,
        EntityKind::CLASS,
        EntityKind::INTERFACE,
        EntityKind::ANNOTATION_INTERFACE,
        EntityKind::FIELD,
        EntityKind::ENUM_MEMBER,
        EntityKind::IMPORT,
        EntityKind::EXPORT,
        EntityKind::ANNOTATION,
    };

    for (const auto kind : nonExecutableKinds) {
        SCOPED_TRACE(static_cast<int>(kind));
        auto node = MakeNode(kind, EntityRole::NORMAL, "declaration");
        AddMethod(&node, "entry.declaration", 0x80U, FileRange {0x800U, 12U});

        EXPECT_TRUE(ExecutableTargetResolver::Resolve(node).empty());
    }
}

TEST(ExecutableTargetResolverTest, ClassRejectsConstructorMethodEvidence)
{
    auto classNode = MakeNode(EntityKind::CLASS, EntityRole::NORMAL, "Vehicle");
    AddMethod(&classNode, "entry.#Vehicle=#Vehicle", 0x88U, FileRange {0x880U, 32U});

    EXPECT_TRUE(ExecutableTargetResolver::Resolve(classNode).empty());
}

TEST(ExecutableTargetResolverTest, DuplicateMethodOffsetsCollapseToOneDeterministicCandidate)
{
    auto firstOrder = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&firstOrder, "zeta", 0x90U, std::nullopt);
    AddMethod(&firstOrder, "", 0x90U, std::nullopt);
    AddMethod(&firstOrder, "alpha", 0x90U, std::nullopt);
    auto reverseOrder = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&reverseOrder, "alpha", 0x90U, std::nullopt);
    AddMethod(&reverseOrder, "", 0x90U, std::nullopt);
    AddMethod(&reverseOrder, "zeta", 0x90U, std::nullopt);

    const auto firstTargets = ExecutableTargetResolver::Resolve(firstOrder);
    const auto reverseTargets = ExecutableTargetResolver::Resolve(reverseOrder);

    ASSERT_EQ(firstTargets.size(), 1U);
    ASSERT_EQ(reverseTargets.size(), 1U);
    EXPECT_EQ(firstTargets[0].methodOffset, 0x90U);
    EXPECT_EQ(firstTargets[0].methodName, "alpha");
    EXPECT_EQ(reverseTargets[0].methodName, firstTargets[0].methodName);
}

TEST(ExecutableTargetResolverTest, DuplicateMethodOffsetAssociatesCodeOnlyForSelectedName)
{
    auto rejectedCode = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&rejectedCode, "zeta", 0x98U, FileRange {0x980U, 12U});
    AddMethod(&rejectedCode, "alpha", 0x98U, std::nullopt);
    auto selectedCode = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&selectedCode, "zeta", 0x98U, std::nullopt);
    AddMethod(&selectedCode, "alpha", 0x98U, FileRange {0x990U, 16U});

    const auto rejectedTargets = ExecutableTargetResolver::Resolve(rejectedCode);
    const auto selectedTargets = ExecutableTargetResolver::Resolve(selectedCode);

    ASSERT_EQ(rejectedTargets.size(), 1U);
    EXPECT_EQ(rejectedTargets[0].methodName, "alpha");
    EXPECT_FALSE(rejectedTargets[0].codeRange.has_value());
    ASSERT_EQ(selectedTargets.size(), 1U);
    EXPECT_EQ(selectedTargets[0].methodName, "alpha");
    ASSERT_TRUE(selectedTargets[0].codeRange.has_value());
    EXPECT_EQ(selectedTargets[0].codeRange->offset, 0x990U);
    EXPECT_EQ(selectedTargets[0].codeRange->size, 16U);
}

TEST(ExecutableTargetResolverTest, DistinctMethodsAreSortedByOffset)
{
    auto item = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&item, "later", 0xB0U, std::nullopt);
    AddMethod(&item, "earlier", 0xA0U, std::nullopt);

    const auto targets = ExecutableTargetResolver::Resolve(item);

    ASSERT_EQ(targets.size(), 2U);
    EXPECT_EQ(targets[0].methodOffset, 0xA0U);
    EXPECT_EQ(targets[0].methodName, "earlier");
    EXPECT_EQ(targets[1].methodOffset, 0xB0U);
    EXPECT_EQ(targets[1].methodName, "later");
}

TEST(ExecutableTargetResolverTest, EmptyPhysicalNameFallsBackToVisibleNodeName)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "visible function");
    AddMethod(&function, "", 0xC0U, std::nullopt);

    const auto targets = ExecutableTargetResolver::Resolve(function);

    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets[0].methodName, "visible function");
}

TEST(ExecutableTargetResolverTest, FallbackNameDoesNotAssociateCodeBacking)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "visible function");
    AddMethod(&function, "", 0xC8U, std::nullopt);
    AddCode(&function, "visible function", FileRange {0xC80U, 20U});

    const auto targets = ExecutableTargetResolver::Resolve(function);

    ASSERT_EQ(targets.size(), 1U);
    EXPECT_EQ(targets[0].methodName, "visible function");
    EXPECT_FALSE(targets[0].codeRange.has_value());
}

TEST(ExecutableTargetResolverTest, UniqueMatchingCodeBackingIsAssociated)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "run");
    AddMethod(&function, "entry.func_main_0", 0xD0U, FileRange {0xD00U, 20U});
    AddCode(&function, "another.method", FileRange {0xE00U, 10U});

    const auto targets = ExecutableTargetResolver::Resolve(function);

    ASSERT_EQ(targets.size(), 1U);
    ASSERT_TRUE(targets[0].codeRange.has_value());
    EXPECT_EQ(targets[0].codeRange->offset, 0xD00U);
    EXPECT_EQ(targets[0].codeRange->size, 20U);
}

TEST(ExecutableTargetResolverTest, AmbiguousMatchingCodeBackingIsNotAssociated)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "run");
    AddMethod(&function, "entry.func_main_0", 0xE0U, FileRange {0xE00U, 20U});
    AddCode(&function, "entry.func_main_0", FileRange {0xF00U, 30U});

    const auto targets = ExecutableTargetResolver::Resolve(function);

    ASSERT_EQ(targets.size(), 1U);
    EXPECT_FALSE(targets[0].codeRange.has_value());
}

TEST(ExecutableTargetResolverTest, SameNamedDistinctMethodsDoNotClaimAnUnownedCodeRange)
{
    auto item = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Unclassified methods");
    AddMethod(&item, "entry.same", 0xE8U, std::nullopt);
    AddMethod(&item, "entry.same", 0xF0U, std::nullopt);
    AddCode(&item, "entry.same", FileRange {0xF00U, 30U});

    const auto targets = ExecutableTargetResolver::Resolve(item);

    ASSERT_EQ(targets.size(), 2U);
    EXPECT_FALSE(targets[0].codeRange.has_value());
    EXPECT_FALSE(targets[1].codeRange.has_value());
}

TEST(ExecutableTargetResolverTest, CodeBackingAloneDoesNotCreateTarget)
{
    auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "run");
    AddCode(&function, "entry.func_main_0", FileRange {0x1000U, 20U});

    EXPECT_TRUE(ExecutableTargetResolver::Resolve(function).empty());
}

TEST(ExecutableTargetResolverTest, NonMethodBackingKindsDoNotCreateTargets)
{
    constexpr std::array nonMethodKinds = {
        PhysicalEntityKind::FILE,          PhysicalEntityKind::RECORD, PhysicalEntityKind::FIELD,
        PhysicalEntityKind::LITERAL_ARRAY, PhysicalEntityKind::CODE,   PhysicalEntityKind::ANNOTATION,
        PhysicalEntityKind::MODULE_DATA,
    };

    for (const auto kind : nonMethodKinds) {
        SCOPED_TRACE(static_cast<int>(kind));
        auto function = MakeNode(EntityKind::FUNCTION, EntityRole::NORMAL, "run");
        function.backing.push_back({kind, FileRange {0x1100U, 20U}, "entry.func_main_0"});

        EXPECT_TRUE(ExecutableTargetResolver::Resolve(function).empty());
    }
}

void VerifyDecodedInstructionHeader(const BytecodeInstructionSafe &raw, const InstructionInfo &instruction,
                                    uint32_t expectedPc, uint64_t expectedFileOffset, uint64_t fileSize)
{
    EXPECT_EQ(instruction.pc, expectedPc);
    EXPECT_GT(instruction.byteSize, 0U);
    EXPECT_LT(instruction.numericOpcode, 0x10000U);
    EXPECT_FALSE(instruction.mnemonic.empty());
    EXPECT_EQ(instruction.fileRange.offset, expectedFileOffset);
    EXPECT_EQ(instruction.fileRange.size, instruction.byteSize);
    EXPECT_TRUE(instruction.fileRange.IsValidFor(fileSize));
    EXPECT_EQ(instruction.byteSize, raw.GetSize());
    EXPECT_EQ(instruction.numericOpcode, static_cast<uint16_t>(raw.GetOpcode()));
    EXPECT_EQ(instruction.canThrow, raw.CanThrow() || raw.HasFlag(BytecodeInstructionSafe::Flags::CONDITIONAL_THROW));
    EXPECT_EQ(instruction.isTerminator, raw.IsTerminator());
}

void VerifyDecodedImmediates(const BytecodeInstructionSafe &raw, const InstructionInfo &instruction)
{
    const auto immediateCount = BytecodeInstructionSafe::HasImm(raw.GetFormat(), 0U) ? raw.GetImmCount() : 0U;
    ASSERT_EQ(instruction.immediates.size(), immediateCount);
    for (size_t operand = 0U; operand < immediateCount; ++operand) {
        const auto type = raw.GetImmType(operand);
        const auto bits = raw.GetImmRaw(operand);
        EXPECT_EQ(instruction.immediates[operand].bitWidth, type.bitWidth);
        EXPECT_EQ(instruction.immediates[operand].rawBits, bits);
        switch (type.kind) {
            case BytecodeImmediateKind::SIGNED:
                EXPECT_EQ(instruction.immediates[operand].kind, ImmediateKind::SIGNED);
                EXPECT_EQ(std::get<int64_t>(instruction.immediates[operand].value), raw.GetImmData(operand));
                break;
            case BytecodeImmediateKind::UNSIGNED:
                EXPECT_EQ(instruction.immediates[operand].kind, ImmediateKind::UNSIGNED);
                EXPECT_EQ(std::get<uint64_t>(instruction.immediates[operand].value), bits);
                break;
            case BytecodeImmediateKind::FLOAT:
                EXPECT_EQ(instruction.immediates[operand].kind, ImmediateKind::FLOAT);
                break;
        }
    }
}

// Coverage flags accumulated across decoded instructions: which operand kinds the walk observed.
struct DecodedOperandCoverage {
    bool sawRegisters {false};
    bool sawImmediates {false};
    bool sawIds {false};
};

void VerifyDecodedIdsAndBytes(const panda_file::File &file, const BytecodeInstructionSafe &raw,
                              const InstructionInfo &instruction, DecodedOperandCoverage &coverage)
{
    std::vector<uint16_t> expectedRegisters;
    for (size_t operand = 0U; BytecodeInstructionSafe::HasVReg(raw.GetFormat(), operand); ++operand) {
        expectedRegisters.push_back(raw.GetVReg(operand));
    }
    std::vector<uint32_t> expectedIds;
    for (size_t operand = 0U; BytecodeInstructionSafe::HasId(raw.GetFormat(), operand); ++operand) {
        expectedIds.push_back(raw.GetId(operand).AsRawValue());
    }
    EXPECT_EQ(std::vector<uint16_t>(instruction.registers.begin(), instruction.registers.end()), expectedRegisters);
    ASSERT_EQ(instruction.ids.size(), expectedIds.size());
    for (size_t operand = 0U; operand < expectedIds.size(); ++operand) {
        EXPECT_EQ(instruction.ids[operand].rawValue, expectedIds[operand]);
    }
    const auto *address = file.GetBase() + instruction.fileRange.offset;
    EXPECT_EQ(std::vector<uint8_t>(instruction.bytes.begin(), instruction.bytes.end()),
              std::vector<uint8_t>(address, address + static_cast<ptrdiff_t>(instruction.byteSize)));
    coverage.sawRegisters = coverage.sawRegisters || !instruction.registers.empty();
    coverage.sawImmediates = coverage.sawImmediates || !instruction.immediates.empty();
    coverage.sawIds = coverage.sawIds || !instruction.ids.empty();
}

TEST_F(FunctionDisassemblerTest, DecodesResolvedFunctionWithStableBoundedInstructionFields)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);

    const auto result = disassembler_.Disassemble(document_->File(), targets.front());

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    EXPECT_EQ(result.disassembly->methodOffset, targets.front().methodOffset);
    const auto &instructions = result.disassembly->instructions;
    ASSERT_FALSE(instructions.empty());

    panda_file::MethodDataAccessor method(
        document_->File(), panda_file::File::EntityId(static_cast<uint32_t>(targets.front().methodOffset)));
    const auto codeId = method.GetCodeId();
    ASSERT_TRUE(codeId.has_value());
    panda_file::CodeDataAccessor code(document_->File(), *codeId);

    EXPECT_EQ(result.disassembly->codeOffset, codeId->GetOffset());
    EXPECT_EQ(result.disassembly->instructionsOffset,
              document_->File().GetIdFromPointer(code.GetInstructions()).GetOffset());
    EXPECT_EQ(result.disassembly->codeSize, code.GetCodeSize());
    EXPECT_EQ(result.disassembly->vregCount, code.GetNumVregs());
    EXPECT_EQ(result.disassembly->argumentCount, code.GetNumArgs());

    uint32_t expectedPc = 0U;
    uint64_t expectedFileOffset = instructions.front().fileRange.offset;
    const uint64_t firstInstructionFileOffset = expectedFileOffset;
    DecodedOperandCoverage coverage;
    for (size_t index = 0U; index < instructions.size(); ++index) {
        const auto &instruction = instructions[index];
        SCOPED_TRACE(index);
        const auto *address = document_->File().GetBase() + instruction.fileRange.offset;
        BytecodeInstructionSafe raw(address, code.GetInstructions(), code.GetInstructions() + code.GetCodeSize() - 1U);
        ASSERT_TRUE(raw.IsValid());
        ASSERT_TRUE(raw.IsOpcodeValid());
        VerifyDecodedInstructionHeader(raw, instruction, expectedPc, expectedFileOffset,
                                       document_->File().GetHeader()->file_size);
        ASSERT_NO_FATAL_FAILURE(VerifyDecodedImmediates(raw, instruction));
        ASSERT_NO_FATAL_FAILURE(VerifyDecodedIdsAndBytes(document_->File(), raw, instruction, coverage));
        expectedPc += instruction.byteSize;
        expectedFileOffset += instruction.byteSize;
    }
    EXPECT_EQ(expectedPc, result.disassembly->codeSize);
    EXPECT_EQ(expectedFileOffset, firstInstructionFileOffset + result.disassembly->codeSize);
    EXPECT_TRUE(coverage.sawRegisters);
    EXPECT_TRUE(coverage.sawImmediates);
    EXPECT_TRUE(coverage.sawIds);
}

TEST_F(FunctionDisassemblerTest, ResolvesRealStringIdAndSnapshotsReadableInstruction)
{
    const auto target = ResolveMethodName("structure_beta.#~@0<#create");
    ASSERT_TRUE(target.has_value());

    const auto result = disassembler_.Disassemble(document_->File(), *target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    const auto found =
        std::find_if(result.disassembly->instructions.begin(), result.disassembly->instructions.end(),
                     [](const InstructionInfo &instruction) { return instruction.fileRange.offset == 0x0aceU; });
    ASSERT_NE(found, result.disassembly->instructions.end());
    ASSERT_EQ(found->ids.size(), 1U);
    EXPECT_EQ(found->ids[0].rawValue, 20U);
    EXPECT_EQ(found->ids[0].resolutionMode, IdResolutionMode::INDEX);
    EXPECT_EQ(found->ids[0].kind, IdOperandKind::STRING);
    ASSERT_TRUE(found->ids[0].resolvedOffset.has_value());
    EXPECT_EQ(found->ids[0].resolvedText, "BetaModel");
    EXPECT_EQ(std::vector<uint8_t>(found->bytes.begin(), found->bytes.end()),
              (std::vector<uint8_t> {0xfeU, 0x09U, 0x14U, 0x00U}));
    EXPECT_EQ(found->fileRange.offset, 0x0aceU);
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(*found), "throw.undefinedifholewithname \"BetaModel\"");
}

TEST_F(FunctionDisassemblerTest, RejectsIdOutsideCurrentMethodIndexSpan)
{
    const auto target = ResolveMethodName("structure_beta.#~@0<#create");
    ASSERT_TRUE(target.has_value());
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {{0x0aceU + 2U, {0xffU, 0xffU}}});
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), *target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("index"), std::string::npos);
}

TEST_F(FunctionDisassemblerTest, DecodesResolvedNamespaceInitializer)
{
    const auto targets = Resolve(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Outer");
    ASSERT_EQ(targets.size(), 1U);

    const auto result = disassembler_.Disassemble(document_->File(), targets.front());

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    EXPECT_FALSE(result.disassembly->instructions.empty());
    EXPECT_TRUE(std::any_of(result.disassembly->instructions.begin(), result.disassembly->instructions.end(),
                            [](const InstructionInfo &instruction) { return instruction.jumpTarget.has_value(); }));
    EXPECT_TRUE(std::any_of(result.disassembly->instructions.begin(), result.disassembly->instructions.end(),
                            [](const InstructionInfo &instruction) { return instruction.isTerminator; }));
}

TEST_F(FunctionDisassemblerTest, IgnoresStaleCodeRangeHintAndUsesAccessorMetadata)
{
    auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    targets.front().codeRange = FileRange {document_->File().GetHeader()->file_size, 1U};

    const auto result = disassembler_.Disassemble(document_->File(), targets.front());

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    EXPECT_FALSE(result.disassembly->instructions.empty());
}

TEST_F(FunctionDisassemblerTest, DecodesResolvedEnumInitializer)
{
    const auto targets = Resolve(EntityKind::ENUM, EntityRole::ENUM_INITIALIZER, "Color");
    ASSERT_EQ(targets.size(), 1U);

    const auto result = disassembler_.Disassemble(document_->File(), targets.front());

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    EXPECT_FALSE(result.disassembly->instructions.empty());
}

TEST_F(FunctionDisassemblerTest, RejectsMethodOffsetOutsideFile)
{
    ExecutableTarget target;
    target.methodOffset = document_->File().GetHeader()->file_size;
    target.methodName = "outside";

    const auto result = disassembler_.Disassemble(document_->File(), target);

    EXPECT_EQ(result.status, DisassemblyStatus::INVALID_METHOD);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_FALSE(result.diagnostic.empty());
}

TEST_F(FunctionDisassemblerTest, RejectsInRangeOffsetThatIsNotAMethodEntity)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    panda_file::MethodDataAccessor method(
        document_->File(), panda_file::File::EntityId(static_cast<uint32_t>(targets.front().methodOffset)));
    const auto codeId = method.GetCodeId();
    ASSERT_TRUE(codeId.has_value());
    auto nonMethod = targets.front();
    nonMethod.methodOffset = codeId->GetOffset();

    const auto result = disassembler_.Disassemble(document_->File(), nonMethod);

    EXPECT_EQ(result.status, DisassemblyStatus::INVALID_METHOD);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReturnsNoCodeForAResolvedMethodWhoseMetadataHasNoCode)
{
    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LNoCode;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *methodName = container.GetOrCreateStringItem("withoutCode");
    auto *returnType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::VOID);
    std::vector<panda_file::MethodParamItem> params;
    auto *prototype = container.GetOrCreateProtoItem(returnType, params);
    auto *method = classItem->AddMethod(methodName, prototype, ACC_PUBLIC | ACC_STATIC, params);
    method->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);

    panda_file::MemoryWriter writer;
    ASSERT_TRUE(container.Write(&writer));
    auto bytes = writer.GetData();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    panda_file::MethodDataAccessor methodData(*file, panda_file::File::EntityId(method->GetOffset()));
    ASSERT_FALSE(methodData.GetCodeId().has_value());
    auto item = MakeNode(EntityKind::ABC_ITEM, EntityRole::ABC_INTERNAL, "Generated no-code method");
    AddMethod(&item, "LNoCode;.withoutCode", method->GetOffset(), std::nullopt);
    const auto targets = ExecutableTargetResolver::Resolve(item);
    ASSERT_EQ(targets.size(), 1U);

    const auto result = disassembler_.Disassemble(*file, targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::NO_CODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_FALSE(result.diagnostic.empty());
    const auto methods = MethodIndex::Build(*file);
    ASSERT_EQ(methods.status, MethodIndexStatus::SUCCESS) << methods.diagnostic;
    const auto *indexed = methods.index->Find(targets.front().methodOffset);
    ASSERT_NE(indexed, nullptr);
    EXPECT_FALSE(indexed->codeOffset.has_value());
}

TEST_F(FunctionDisassemblerTest, ReportsOverflowingMethodAccessFlagsAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    constexpr uint64_t fixedMethodHeaderSize = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
    const auto accessFlagsOffset = targets.front().methodOffset + fixedMethodHeaderSize;
    ASSERT_LE(accessFlagsOffset + 5U, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path,
                                  std::vector<BytePatch> {{accessFlagsOffset, {0x80U, 0x80U, 0x80U, 0x80U, 0x80U}}});
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto methods = MethodIndex::Build(opened.Value()->File());
    EXPECT_EQ(methods.status, MethodIndexStatus::MALFORMED_FILE);
    EXPECT_EQ(methods.index, nullptr);

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsInvalidMethodMetadataTagAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto tagOffset = MethodTaggedValuesOffset(document_->File(), targets.front());
    ASSERT_LT(tagOffset, document_->File().GetHeader()->file_size);
    ASSERT_EQ(document_->File().GetBase()[tagOffset], static_cast<uint8_t>(panda_file::MethodTag::CODE));
    TemporaryPatchedAbc malformed(document_->Summary().path, tagOffset, 0xFFU);
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsCodeReferenceOutsideFileAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto tagOffset = MethodTaggedValuesOffset(document_->File(), targets.front());
    ASSERT_LE(tagOffset + 1U + sizeof(uint32_t), document_->File().GetHeader()->file_size);
    ASSERT_EQ(document_->File().GetBase()[tagOffset], static_cast<uint8_t>(panda_file::MethodTag::CODE));
    const auto outsideCodeId = EncodeUint32(document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {{tagOffset + 1U, outsideCodeId}});
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsUnterminatedCodeHeaderUlebAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    ASSERT_LE(layout.codeOffset + 5U, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path,
                                  std::vector<BytePatch> {{layout.codeOffset, {0x80U, 0x80U, 0x80U, 0x80U, 0x80U}}});
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsUnterminatedTryBlockUlebAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    ASSERT_EQ(document_->File().GetBase()[layout.triesSizeFieldOffset], 0U);
    ASSERT_LE(layout.instructionsEndOffset + 5U, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path,
                                  std::vector<BytePatch> {
                                      {layout.triesSizeFieldOffset, {1U}},
                                      {layout.instructionsEndOffset, {0x80U, 0x80U, 0x80U, 0x80U, 0x80U}},
                                  });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsUnterminatedCatchBlockUlebAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    ASSERT_EQ(document_->File().GetBase()[layout.triesSizeFieldOffset], 0U);
    ASSERT_LE(layout.instructionsEndOffset + 8U, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path,
                                  std::vector<BytePatch> {
                                      {layout.triesSizeFieldOffset, {1U}},
                                      {layout.instructionsEndOffset, {0U, 1U, 1U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U}},
                                  });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsTryRangeOutsideCodeAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, 0U);
    ASSERT_LE(layout.instructionsEndOffset + tryBlock.size(), document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {
                                                                 {layout.triesSizeFieldOffset, {1U}},
                                                                 {layout.instructionsEndOffset, tryBlock},
                                                             });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsCatchHandlerOutsideCodeAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 1U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 0U);
    ASSERT_LE(layout.instructionsEndOffset + tryBlock.size(), document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {
                                                                 {layout.triesSizeFieldOffset, {1U}},
                                                                 {layout.instructionsEndOffset, tryBlock},
                                                             });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsTryEndpointInsideInstructionAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Outer");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    const auto interiorOffset = FirstJumpInteriorBytecodeOffset(document_->File(), targets.front());
    ASSERT_LT(interiorOffset, layout.codeSize);
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, interiorOffset);
    AppendUleb32(&tryBlock, 0U);
    ASSERT_LE(layout.instructionsEndOffset + tryBlock.size(), document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {
                                                                 {layout.triesSizeFieldOffset, {1U}},
                                                                 {layout.instructionsEndOffset, tryBlock},
                                                             });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsCatchHandlerInsideInstructionAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Outer");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    const auto interiorOffset = FirstJumpInteriorBytecodeOffset(document_->File(), targets.front());
    ASSERT_LT(interiorOffset, layout.codeSize);
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 1U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, interiorOffset);
    AppendUleb32(&tryBlock, 0U);
    ASSERT_LE(layout.instructionsEndOffset + tryBlock.size(), document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc malformed(document_->Summary().path, std::vector<BytePatch> {
                                                                 {layout.triesSizeFieldOffset, {1U}},
                                                                 {layout.instructionsEndOffset, tryBlock},
                                                             });
    auto opened = AbcDocument::Open(malformed.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, DecodesCodeWithBoundedExceptionTable)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 1U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, 0U);
    ASSERT_LE(layout.instructionsEndOffset + tryBlock.size(), document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc withExceptionTable(document_->Summary().path, std::vector<BytePatch> {
                                                                          {layout.triesSizeFieldOffset, {1U}},
                                                                          {layout.instructionsEndOffset, tryBlock},
                                                                      });
    auto opened = AbcDocument::Open(withExceptionTable.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    EXPECT_FALSE(result.disassembly->instructions.empty());
    ASSERT_EQ(result.disassembly->tryRegions.size(), 1U);
    const auto &tryRegion = result.disassembly->tryRegions.front();
    EXPECT_EQ(tryRegion.startPc, 0U);
    EXPECT_EQ(tryRegion.endPc, layout.codeSize);
    ASSERT_EQ(tryRegion.handlers.size(), 1U);
    EXPECT_EQ(tryRegion.handlers[0].typeIndex, 0U);
    EXPECT_EQ(tryRegion.handlers[0].handlerPc, 0U);
    EXPECT_EQ(tryRegion.handlers[0].handlerSize, 0U);
    EXPECT_EQ(tryRegion.handlers[0].order, 0U);
}

TEST_F(FunctionDisassemblerTest, ReportsInvalidOpcodeAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    panda_file::MethodDataAccessor method(
        document_->File(), panda_file::File::EntityId(static_cast<uint32_t>(targets.front().methodOffset)));
    const auto codeId = method.GetCodeId();
    ASSERT_TRUE(codeId.has_value());
    panda_file::CodeDataAccessor code(document_->File(), *codeId);
    const auto instructionOffset = document_->File().GetIdFromPointer(code.GetInstructions()).GetOffset();
    TemporaryPatchedAbc invalidOpcode(document_->Summary().path, instructionOffset, 0xFFU);
    auto opened = AbcDocument::Open(invalidOpcode.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsInstructionCrossingCodeRangeAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto codeSizeOffset = CodeSizeFieldOffset(document_->File(), targets.front());
    ASSERT_LT(codeSizeOffset, document_->File().GetHeader()->file_size);
    ASSERT_EQ(document_->File().GetBase()[codeSizeOffset] & 0x80U, 0U);
    ASSERT_GT(document_->File().GetBase()[codeSizeOffset], 1U);
    TemporaryPatchedAbc truncated(document_->Summary().path, codeSizeOffset, 1U);
    auto opened = AbcDocument::Open(truncated.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsJumpOutsideCodeRangeAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Outer");
    ASSERT_EQ(targets.size(), 1U);
    const auto immediateOffset = FirstJumpImmediateOffset(document_->File(), targets.front());
    ASSERT_LT(immediateOffset, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc invalidJump(document_->Summary().path, immediateOffset, 0x7FU);
    auto opened = AbcDocument::Open(invalidJump.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, ReportsJumpIntoMiddleOfInstructionAsMalformedBytecode)
{
    const auto targets = Resolve(EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER, "Outer");
    ASSERT_EQ(targets.size(), 1U);
    const auto immediateOffset = FirstJumpImmediateOffset(document_->File(), targets.front());
    ASSERT_LT(immediateOffset, document_->File().GetHeader()->file_size);
    TemporaryPatchedAbc invalidJump(document_->Summary().path, immediateOffset, 1U);
    auto opened = AbcDocument::Open(invalidJump.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    const auto result = disassembler_.Disassemble(opened.Value()->File(), targets.front());

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, MethodIndexBuildsValidatedCodeMetadataForReuse)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);

    const auto built = MethodIndex::Build(document_->File());

    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    const auto *method = built.index->Find(targets.front().methodOffset);
    ASSERT_NE(method, nullptr);
    ASSERT_TRUE(method->codeOffset.has_value());
    panda_file::MethodDataAccessor accessor(
        document_->File(), panda_file::File::EntityId(static_cast<uint32_t>(targets.front().methodOffset)));
    ASSERT_TRUE(accessor.GetCodeId().has_value());
    EXPECT_EQ(*method->codeOffset, accessor.GetCodeId()->GetOffset());

    const auto result = disassembler_.Disassemble(document_->File(), *built.index, targets.front());
    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
}

TEST_F(FunctionDisassemblerTest, MethodIndexBuildCanBeCancelledWithoutBackendAccessAfterCheck)
{
    CancellationToken token([count = size_t {0U}]() mutable { return ++count >= 1U; });

    const auto built = MethodIndex::Build(document_->File(), token);

    EXPECT_EQ(built.status, MethodIndexStatus::CANCELLED);
    EXPECT_EQ(built.index, nullptr);
}

TEST_F(FunctionDisassemblerTest, MethodIndexBuildObservesCancellationDuringSafeWalk)
{
    auto checks = std::make_shared<std::atomic<size_t>>(0U);
    CancellationToken token([checks] { return ++*checks >= 10U; });

    const auto built = MethodIndex::Build(document_->File(), token);

    EXPECT_EQ(built.status, MethodIndexStatus::CANCELLED);
    EXPECT_EQ(built.index, nullptr);
    EXPECT_GE(checks->load(), 10U);
}

TEST(MethodIndexTest, RejectsInvalidFifthByteInSignedFieldValue)
{
    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LSignedField;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *fieldName = container.GetOrCreateStringItem("value");
    auto *fieldType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::I32);
    auto *field = classItem->AddField(fieldName, fieldType, ACC_PUBLIC | ACC_STATIC);
    field->SetValue(container.GetOrCreateIntegerValueItem(0x7FFFFFFFU));

    panda_file::MemoryWriter writer;
    ASSERT_TRUE(container.Write(&writer));
    auto bytes = writer.GetData();
    constexpr size_t fieldFixedHeaderSize = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
    const size_t valueTagOffset = field->GetOffset() + fieldFixedHeaderSize + 1U;
    ASSERT_LT(valueTagOffset + 5U, bytes.size());
    ASSERT_EQ(bytes[valueTagOffset], static_cast<uint8_t>(panda_file::FieldTag::INT_VALUE));
    ASSERT_EQ(bytes[valueTagOffset + 5U], 0x07U);
    bytes[valueTagOffset + 5U] = 0x08U;
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto built = MethodIndex::Build(*file);

    EXPECT_EQ(built.status, MethodIndexStatus::MALFORMED_FILE);
    EXPECT_EQ(built.index, nullptr);
}

TEST(MethodIndexTest, AcceptsWriterAndAccessorRepeatedMethodAnnotations)
{
    auto fixture = CreateRepeatedMethodAnnotationFixture();
    ASSERT_FALSE(fixture.bytes.empty());
    ASSERT_LT(fixture.secondTagOffset + sizeof(panda_file::File::EntityId), fixture.bytes.size());
    EXPECT_EQ(fixture.bytes[fixture.firstTagOffset], static_cast<uint8_t>(panda_file::MethodTag::ANNOTATION));
    EXPECT_EQ(fixture.bytes[fixture.secondTagOffset], static_cast<uint8_t>(panda_file::MethodTag::ANNOTATION));
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    panda_file::MethodDataAccessor accessor(*file, panda_file::File::EntityId(fixture.methodOffset));
    std::vector<uint32_t> annotationOffsets;
    struct AnnotationOffsetCollector {
        std::vector<uint32_t> *offsets;
        void operator()(panda_file::File::EntityId id) const
        {
            offsets->push_back(id.GetOffset());
        }
    };
    accessor.EnumerateAnnotations(AnnotationOffsetCollector {&annotationOffsets});
    ASSERT_EQ(annotationOffsets.size(), 2U);
    EXPECT_EQ(annotationOffsets[0], fixture.firstAnnotationOffset);
    EXPECT_EQ(annotationOffsets[1], fixture.secondAnnotationOffset);

    const auto built = MethodIndex::Build(*file);
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    EXPECT_NE(built.index->Find(fixture.methodOffset), nullptr);
}

TEST(MethodIndexTest, RejectsOutOfOrderMethodTagSections)
{
    auto fixture = CreateRepeatedMethodAnnotationFixture();
    ASSERT_LT(fixture.firstTagOffset, fixture.bytes.size());
    fixture.bytes[fixture.firstTagOffset] = static_cast<uint8_t>(panda_file::MethodTag::PARAM_ANNOTATION);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto built = MethodIndex::Build(*file);

    EXPECT_EQ(built.status, MethodIndexStatus::MALFORMED_FILE);
    EXPECT_EQ(built.index, nullptr);
    EXPECT_EQ(built.diagnostic, "METHOD tagged sections are out of order");
}

TEST(MethodIndexTest, RejectsDuplicateUniqueMethodTagSection)
{
    auto fixture = CreateRepeatedMethodAnnotationFixture();
    ASSERT_LT(fixture.secondTagOffset, fixture.bytes.size());
    fixture.bytes[fixture.firstTagOffset] = static_cast<uint8_t>(panda_file::MethodTag::DEBUG_INFO);
    fixture.bytes[fixture.secondTagOffset] = static_cast<uint8_t>(panda_file::MethodTag::DEBUG_INFO);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto built = MethodIndex::Build(*file);

    EXPECT_EQ(built.status, MethodIndexStatus::MALFORMED_FILE);
    EXPECT_EQ(built.index, nullptr);
    EXPECT_EQ(built.diagnostic, "METHOD optional tag is duplicated");
}

TEST(MethodIndexTest, RejectsBothFieldValueTagKinds)
{
    auto fixture = CreateFieldValueAnnotationFixture();
    ASSERT_LT(fixture.annotationTagOffset, fixture.bytes.size());
    ASSERT_EQ(fixture.bytes[fixture.valueTagOffset], static_cast<uint8_t>(panda_file::FieldTag::INT_VALUE));
    ASSERT_EQ(fixture.bytes[fixture.annotationTagOffset], static_cast<uint8_t>(panda_file::FieldTag::ANNOTATION));
    fixture.bytes[fixture.annotationTagOffset] = static_cast<uint8_t>(panda_file::FieldTag::VALUE);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto built = MethodIndex::Build(*file);

    EXPECT_EQ(built.status, MethodIndexStatus::MALFORMED_FILE);
    EXPECT_EQ(built.index, nullptr);
    EXPECT_EQ(built.diagnostic, "FIELD value tags are mutually exclusive");
}

TEST_F(FunctionDisassemblerTest, CodeByteLimitRejectsOverLimitAndAcceptsBoundary)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    ASSERT_GT(layout.codeSize, 0U);
    DisassemblyLimits limits;
    limits.maxCodeBytes = layout.codeSize - 1U;

    const auto rejected = disassembler_.Disassemble(document_->File(), targets.front(), limits);
    limits.maxCodeBytes = layout.codeSize;
    const auto accepted = disassembler_.Disassemble(document_->File(), targets.front(), limits);

    EXPECT_EQ(rejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(accepted.status, DisassemblyStatus::SUCCESS) << accepted.diagnostic;
}

TEST_F(FunctionDisassemblerTest, InstructionCountLimitRejectsOverLimitAndAcceptsBoundary)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto baseline = disassembler_.Disassemble(document_->File(), targets.front());
    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    ASSERT_GT(baseline.disassembly->instructions.size(), 0U);
    DisassemblyLimits limits;
    limits.maxInstructionCount = baseline.disassembly->instructions.size() - 1U;

    const auto rejected = disassembler_.Disassemble(document_->File(), targets.front(), limits);
    limits.maxInstructionCount = baseline.disassembly->instructions.size();
    const auto accepted = disassembler_.Disassemble(document_->File(), targets.front(), limits);

    EXPECT_EQ(rejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(accepted.status, DisassemblyStatus::SUCCESS) << accepted.diagnostic;
}

TEST_F(FunctionDisassemblerTest, TryAndCatchLimitsRejectOverLimitAndAcceptBoundaries)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto layout = GetCodeLayout(document_->File(), targets.front());
    std::vector<uint8_t> tryBlock;
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, layout.codeSize);
    AppendUleb32(&tryBlock, 1U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, 0U);
    AppendUleb32(&tryBlock, 0U);
    TemporaryPatchedAbc patched(document_->Summary().path, std::vector<BytePatch> {
                                                               {layout.triesSizeFieldOffset, {1U}},
                                                               {layout.instructionsEndOffset, tryBlock},
                                                           });
    auto opened = AbcDocument::Open(patched.Path());
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();

    DisassemblyLimits limits;
    limits.maxTryBlockCount = 0U;
    const auto triesRejected = disassembler_.Disassemble(opened.Value()->File(), targets.front(), limits);
    limits.maxTryBlockCount = 1U;
    limits.maxTotalCatchCount = 0U;
    const auto catchesRejected = disassembler_.Disassemble(opened.Value()->File(), targets.front(), limits);
    limits.maxTotalCatchCount = 1U;
    const auto accepted = disassembler_.Disassemble(opened.Value()->File(), targets.front(), limits);

    EXPECT_EQ(triesRejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(catchesRejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(accepted.status, DisassemblyStatus::SUCCESS) << accepted.diagnostic;
}

TEST_F(FunctionDisassemblerTest, FormattedTextLimitRejectsOverLimitAndAcceptsBoundary)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto baseline = disassembler_.Disassemble(document_->File(), targets.front());
    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    size_t formattedBytes = 0U;
    for (const auto &instruction : baseline.disassembly->instructions) {
        formattedBytes += instruction.mnemonic.size() + instruction.operands.size();
        formattedBytes += instruction.operands.empty() ? 0U : 1U;
    }
    ASSERT_GT(formattedBytes, 0U);
    DisassemblyLimits limits;
    limits.maxFormattedTextBytes = formattedBytes - 1U;

    const auto rejected = disassembler_.Disassemble(document_->File(), targets.front(), limits);
    limits.maxFormattedTextBytes = formattedBytes;
    const auto accepted = disassembler_.Disassemble(document_->File(), targets.front(), limits);

    EXPECT_EQ(rejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    EXPECT_EQ(accepted.status, DisassemblyStatus::SUCCESS) << accepted.diagnostic;
}

TEST_F(FunctionDisassemblerTest, ModelLimitRejectsBelowRetainedCapacity)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    const auto baseline = disassembler_.Disassemble(document_->File(), targets.front());
    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    const auto modelBytes = FunctionDisassembler::EstimateModelBytes(*baseline.disassembly);
    ASSERT_GT(modelBytes, 0U);
    DisassemblyLimits limits;
    limits.maxModelBytes = modelBytes - 1U;

    const auto rejected = disassembler_.Disassemble(document_->File(), targets.front(), limits);
    EXPECT_EQ(rejected.status, DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
}

TEST_F(FunctionDisassemblerTest, MaximalConfiguredLimitsDoNotOverflowCapacityPrechecks)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    DisassemblyLimits limits;
    limits.maxCodeBytes = std::numeric_limits<uint64_t>::max();
    limits.maxInstructionCount = std::numeric_limits<uint64_t>::max();
    limits.maxTryBlockCount = std::numeric_limits<uint64_t>::max();
    limits.maxTotalCatchCount = std::numeric_limits<uint64_t>::max();
    limits.maxFormattedTextBytes = std::numeric_limits<uint64_t>::max();
    limits.maxModelBytes = std::numeric_limits<uint64_t>::max();

    const auto result = disassembler_.Disassemble(document_->File(), targets.front(), limits);

    EXPECT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
}

TEST_F(FunctionDisassemblerTest, RunningProductionDecodeObservesCancellation)
{
    const auto targets = Resolve(EntityKind::FUNCTION, EntityRole::NORMAL, "alphaEntry");
    ASSERT_EQ(targets.size(), 1U);
    auto built = MethodIndex::Build(document_->File());
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    auto reachedDecodeLoop = std::make_shared<std::promise<void>>();
    auto reachedFuture = reachedDecodeLoop->get_future();
    std::promise<void> releaseCheck;
    auto releaseFuture = releaseCheck.get_future().share();
    auto checks = std::make_shared<std::atomic<size_t>>(0U);
    CancellationToken token([checks, reachedDecodeLoop, releaseFuture] {
        const auto check = ++*checks;
        if (check == 3U) {
            reachedDecodeLoop->set_value();
            releaseFuture.wait();
        }
        return check >= 3U;
    });

    auto future = std::async(std::launch::async, [this, target = targets.front(), token, built = std::move(built)] {
        return disassembler_.Disassemble(document_->File(), *built.index, target, DisassemblyLimits {}, token);
    });
    reachedFuture.wait();
    releaseCheck.set_value();
    const auto result = future.get();

    EXPECT_EQ(result.status, DisassemblyStatus::CANCELLED);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST(DisassemblyFormatterTest, FormatsStructuredColumnsAndRequiredTooltipDetails)
{
    InstructionInfo instruction;
    instruction.pc = 0x10U;
    instruction.byteSize = 3U;
    instruction.numericOpcode = 0x12AU;
    instruction.registers = {1U, 7U};
    instruction.immediates = {
        {ImmediateKind::SIGNED, 8U, 0xfeU, int64_t {-2}},
        {ImmediateKind::UNSIGNED, 8U, 0x8eU, uint64_t {142U}},
        {ImmediateKind::FLOAT, 64U, 0x40091eb851eb851fULL, double {3.14}},
    };
    instruction.ids = {
        {IdOperandKind::STRING, IdResolutionMode::INDEX, 3U, 0x200U, "hello"},
        {IdOperandKind::METHOD, IdResolutionMode::INDEX, 99U, 0x300U, "target"},
    };
    instruction.jumpTarget = 0x20U;
    instruction.fileRange = {0x100U, 3U};
    instruction.canThrow = true;
    instruction.isTerminator = false;

    EXPECT_EQ(DisassemblyFormatter::FormatPc(instruction.pc), "0x00000010");
    EXPECT_EQ(DisassemblyFormatter::FormatOpcode(instruction.numericOpcode), "0x012A");
    EXPECT_EQ(DisassemblyFormatter::FormatRegisters(instruction.registers), "v1, v7");
    EXPECT_EQ(DisassemblyFormatter::FormatJumpTarget(instruction.jumpTarget), "0x00000020");
    const auto tooltip = DisassemblyFormatter::FormatTooltip(instruction);
    EXPECT_NE(tooltip.find("Size: 3 bytes"), std::string::npos);
    EXPECT_NE(tooltip.find("Numeric opcode: 0x012A"), std::string::npos);
    EXPECT_NE(tooltip.find("ABC range: [0x100, 0x103)"), std::string::npos);
    EXPECT_NE(tooltip.find("Immediates: i8 -2 (raw 0xFE), u8 142 (raw 0x8E), "
                           "f64 3.14 (raw 0x40091EB851EB851F)"),
              std::string::npos);
    EXPECT_NE(tooltip.find("IDs: string #3 -> 0x00000200 \"hello\", "
                           "method #99 -> 0x00000300 target@0x00000300"),
              std::string::npos);
    EXPECT_NE(tooltip.find("Can throw: yes"), std::string::npos);
    EXPECT_NE(tooltip.find("Terminator: no"), std::string::npos);
}

TEST(DisassemblyFormatterTest, ReportsOverflowingExternalFileRangeWithoutWrapping)
{
    InstructionInfo instruction;
    instruction.fileRange = {std::numeric_limits<uint64_t>::max(), 2U};

    const auto tooltip = DisassemblyFormatter::FormatTooltip(instruction);

    EXPECT_NE(tooltip.find("ABC range: overflow"), std::string::npos);
}

TEST(DisassemblyFormatterTest, PreservesFullRawValueForUnknownDirectId)
{
    const IdOperand unknown {IdOperandKind::UNKNOWN, IdResolutionMode::DIRECT_OFFSET, 0x12345678U, std::nullopt, {}};

    EXPECT_EQ(DisassemblyFormatter::FormatIdOperand(unknown), "id305419896");
    InstructionInfo instruction;
    instruction.ids.push_back(unknown);
    EXPECT_NE(DisassemblyFormatter::FormatTooltip(instruction).find("unknown #305419896"), std::string::npos);
}

TEST(DisassemblyFormatterTest, FormatsFunctionHeaderWithPhysicalOffsetsAndDynamicArgumentSlots)
{
    FunctionDisassembly disassembly;
    disassembly.methodOffset = 0x123U;
    disassembly.codeOffset = 0x456U;
    disassembly.instructionsOffset = 0x460U;
    disassembly.codeSize = 0x20U;
    disassembly.vregCount = 7U;
    disassembly.argumentCount = 3U;

    EXPECT_EQ(DisassemblyFormatter::FormatFunctionHeader(disassembly),
              "METHOD metadata offset: 0x00000123\n"
              "CODE item offset: 0x00000456\n"
              "Bytecode instruction ABC range: [0x00000460, 0x00000480) (32 bytes)\n"
              "Local vregs: 7\n"
              "ABC argument slots: 3 (source 0 + implicit 3: CallTarget, NewTarget, this)");

    disassembly.argumentCount = 5U;
    EXPECT_NE(DisassemblyFormatter::FormatFunctionHeader(disassembly)
                  .find("ABC argument slots: 5 (source 2 + implicit 3: CallTarget, NewTarget, this)"),
              std::string::npos);
}

TEST(DisassemblyFormatterTest, ReportsAllSubImplicitArgumentCountsWithoutUnderflow)
{
    FunctionDisassembly disassembly;
    for (uint32_t count = 0U; count < 3U; ++count) {
        disassembly.argumentCount = count;
        const auto header = DisassemblyFormatter::FormatFunctionHeader(disassembly);
        const auto expected = "ABC argument slots: " + std::to_string(count);
        EXPECT_NE(header.find(expected), std::string::npos) << "count=" << count;
        EXPECT_NE(header.find("anomalous"), std::string::npos) << "count=" << count;
        EXPECT_EQ(header.find("source 429496"), std::string::npos) << "count=" << count;
    }
}

TEST(DisassemblyFormatterTest, FormatsInstructionColumnsAndFoldedStructuredDetails)
{
    InstructionInfo instruction;
    instruction.pc = 0x10U;
    instruction.byteSize = 4U;
    instruction.numericOpcode = 0xFE09U;
    instruction.mnemonic = "throw.undefinedifholewithname";
    instruction.operands = "\"BetaModel\"";
    instruction.bytes = {0xFEU, 0x09U, 0x14U, 0x00U};
    instruction.registers = {1U, 7U};
    instruction.immediates = {{ImmediateKind::SIGNED, 8U, 0xfeU, int64_t {-2}}};
    instruction.ids = {
        {IdOperandKind::STRING, IdResolutionMode::INDEX, 20U, 0x300U, "BetaModel"},
        {IdOperandKind::METHOD, IdResolutionMode::DIRECT_OFFSET, 0x400U, 0x400U, "target"},
    };
    instruction.fileRange = {0x210U, 4U};
    instruction.canThrow = true;
    instruction.isTerminator = false;

    EXPECT_EQ(DisassemblyFormatter::FormatInstructionAbcOffset(0x200U, instruction.pc), "0x00000210");
    EXPECT_EQ(DisassemblyFormatter::FormatBytes(instruction.bytes), "FE 09 14 00");
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(instruction), "throw.undefinedifholewithname \"BetaModel\"");

    const auto details = DisassemblyFormatter::FormatInstructionDetails(instruction, 7U);
    EXPECT_NE(details.find("Encoded byte size: 4"), std::string::npos);
    EXPECT_NE(details.find("Numeric opcode: 0xFE09"), std::string::npos);
    EXPECT_NE(details.find("Registers: v1 (raw 1), a0 (raw 7)"), std::string::npos);
    EXPECT_NE(details.find("Immediates: i8 -2 (raw 0xFE)"), std::string::npos);
    EXPECT_NE(details.find("string ID: raw 20, mode index, resolved ABC offset 0x00000300, display \"BetaModel\""),
              std::string::npos);
    EXPECT_NE(details.find("method ID: raw 1024, mode direct offset, resolved ABC offset 0x00000400, "
                           "display target@0x00000400"),
              std::string::npos);
    EXPECT_NE(details.find("ABC file range: [0x00000210, 0x00000214)"), std::string::npos);
    EXPECT_NE(details.find("Can throw: yes"), std::string::npos);
    EXPECT_NE(details.find("Terminator: no"), std::string::npos);
}

TEST(DisassemblyFormatterTest, KeepsEmptyAndNonTextIdDisplaysDistinctFromUnresolvedOffsets)
{
    InstructionInfo instruction;
    instruction.ids = {
        {IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x100U, ""},
        {IdOperandKind::LITERAL_ARRAY, IdResolutionMode::DIRECT_OFFSET, 0x200U, 0x200U, ""},
        {IdOperandKind::METHOD, IdResolutionMode::INDEX, 3U, std::nullopt, ""},
        {IdOperandKind::STRING, IdResolutionMode::INDEX, 4U, std::nullopt, "stale"},
        {IdOperandKind::METHOD, IdResolutionMode::INDEX, 5U, 0x500U, ""},
        {IdOperandKind::UNKNOWN, IdResolutionMode::DIRECT_OFFSET, 6U, std::nullopt, ""},
    };

    const auto details = DisassemblyFormatter::FormatInstructionDetails(instruction, 0U);
    EXPECT_NE(details.find("string ID: raw 1, mode index, resolved ABC offset 0x00000100, display \"\""),
              std::string::npos);
    EXPECT_NE(details.find("literal array ID: raw 512, mode direct offset, resolved ABC offset 0x00000200, "
                           "display literal-array@0x00000200"),
              std::string::npos);
    EXPECT_NE(details.find("method ID: raw 3, mode index, resolved ABC offset unavailable, display unavailable"),
              std::string::npos);
    EXPECT_NE(details.find("string ID: raw 4, mode index, resolved ABC offset unavailable, display unavailable"),
              std::string::npos);
    EXPECT_NE(details.find("method ID: raw 5, mode index, resolved ABC offset 0x00000500, "
                           "display @0x00000500"),
              std::string::npos);
    EXPECT_NE(details.find("unknown ID: raw 6, mode direct offset, resolved ABC offset unavailable, display id6"),
              std::string::npos);
}

TEST(DisassemblyFormatterTest, HandlesExactOffsetBoundariesAndOverflow)
{
    constexpr auto maxOffset = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(DisassemblyFormatter::FormatInstructionAbcOffset(maxOffset - 1U, 1U), "0xFFFFFFFFFFFFFFFF");
    EXPECT_EQ(DisassemblyFormatter::FormatInstructionAbcOffset(maxOffset, 1U), "overflow");

    FunctionDisassembly disassembly;
    disassembly.instructionsOffset = maxOffset - 2U;
    disassembly.codeSize = 2U;
    EXPECT_NE(DisassemblyFormatter::FormatFunctionHeader(disassembly)
                  .find("Bytecode instruction ABC range: [0xFFFFFFFFFFFFFFFD, 0xFFFFFFFFFFFFFFFF) (2 bytes)"),
              std::string::npos);
    disassembly.codeSize = 3U;
    EXPECT_NE(DisassemblyFormatter::FormatFunctionHeader(disassembly)
                  .find("Bytecode instruction ABC range: [0xFFFFFFFFFFFFFFFD, overflow) (3 bytes)"),
              std::string::npos);

    InstructionInfo instruction;
    instruction.fileRange = {maxOffset - 2U, 2U};
    EXPECT_NE(DisassemblyFormatter::FormatInstructionDetails(instruction, 0U)
                  .find("ABC file range: [0xFFFFFFFFFFFFFFFD, 0xFFFFFFFFFFFFFFFF)"),
              std::string::npos);
    instruction.fileRange.size = 3U;
    EXPECT_NE(DisassemblyFormatter::FormatInstructionDetails(instruction, 0U).find("ABC file range: overflow"),
              std::string::npos);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
