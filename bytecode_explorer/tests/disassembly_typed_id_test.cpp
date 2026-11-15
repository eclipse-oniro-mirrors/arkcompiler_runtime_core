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

std::string_view AsStringView(const ModelString &value)
{
    return {value.data(), value.size()};
}

void WriteUint32(std::vector<uint8_t> *bytes, uint64_t offset, uint32_t value)
{
    ASSERT_NE(bytes, nullptr);
    const auto encoded = EncodeUint32(value);
    ASSERT_LE(offset + encoded.size(), bytes->size());
    std::copy(encoded.begin(), encoded.end(), bytes->begin() + static_cast<ptrdiff_t>(offset));
}

struct GeneratedTypedIdFixture {
    std::vector<uint8_t> bytes;
    uint32_t entryMethodOffset {0U};
    uint32_t targetMethodOffset {0U};
    uint32_t literalArrayOffset {0U};
    uint16_t methodIndex {0U};
    uint16_t literalArrayIndex {0U};
};

class ScopedItemContainerApi final {
public:
    explicit ScopedItemContainerApi(uint8_t api)
        : previousApi_(panda_file::ItemContainer::GetApi()), previousSubApi_(panda_file::ItemContainer::GetSubApi())
    {
        panda_file::ItemContainer::SetApi(api);
        panda_file::ItemContainer::SetSubApi({});
    }

    ~ScopedItemContainerApi()
    {
        panda_file::ItemContainer::SetApi(previousApi_);
        panda_file::ItemContainer::SetSubApi(previousSubApi_);
    }

    NO_COPY_SEMANTIC(ScopedItemContainerApi);
    NO_MOVE_SEMANTIC(ScopedItemContainerApi);

private:
    uint8_t previousApi_ {0U};
    std::string previousSubApi_;
};

struct GeneratedDirectStringFixture {
    std::vector<uint8_t> bytes;
    uint32_t methodOffset {0U};
    uint32_t stringOffset {0U};
    std::string text;
};

struct GeneratedControlFlowFixture {
    std::vector<uint8_t> bytes;
    uint32_t methodOffset {0U};
};

GeneratedControlFlowFixture CreateGeneratedControlFlowFixture(std::vector<uint8_t> instructions = {},
                                                              uint32_t protectedSize = 0U)
{
    using Opcode = BytecodeInstructionSafe::Opcode;
    const auto opcodeByte = [](Opcode opcode) { return static_cast<uint8_t>(static_cast<uint16_t>(opcode)); };
    if (instructions.empty()) {
        instructions = {
            opcodeByte(Opcode::JNEZ_IMM8),
            4U,
            opcodeByte(Opcode::JMP_IMM8),
            2U,
            opcodeByte(Opcode::RETURNUNDEFINED),
            opcodeByte(Opcode::THROW_PREF_NONE),
            0U,
        };
    }

    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LControlFlow;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *returnType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::VOID);
    std::vector<panda_file::MethodParamItem> params;
    auto *prototype = container.GetOrCreateProtoItem(returnType, params);
    auto *methodName = container.GetOrCreateStringItem("controlFlow");
    auto *method = classItem->AddMethod(methodName, prototype, ACC_PUBLIC | ACC_STATIC, params);
    method->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *code = container.CreateItem<panda_file::CodeItem>(0U, 0U, std::move(instructions));
    method->SetCode(code);
    if (protectedSize != 0U) {
        code->SetNumVregs(1U);
        code->AddTryBlock(panda_file::CodeItem::TryBlock(
            0U, protectedSize, {panda_file::CodeItem::CatchBlock(method, nullptr, protectedSize + 1U, 2U)}));
    }

    panda_file::MemoryWriter writer;
    if (!container.Write(&writer)) {
        ADD_FAILURE() << "Failed to serialize control-flow fixture";
        return {};
    }
    return {writer.GetData(), method->GetOffset()};
}

GeneratedDirectStringFixture CreateGeneratedDirectStringFixture(size_t fillerSize, std::string text)
{
    std::vector<uint8_t> instructions {0xfcU, 0x23U, 0U, 0U, 0U, 0U};  // deprecated.getmodulenamespace ID32
    panda_file::ItemContainer container;
    auto *classItem = container.GetOrCreateClassItem("LDirectStringId;");
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    auto *returnType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::VOID);
    std::vector<panda_file::MethodParamItem> params;
    auto *prototype = container.GetOrCreateProtoItem(returnType, params);
    auto *methodName = container.GetOrCreateStringItem("directString");
    auto *method = classItem->AddMethod(methodName, prototype, ACC_PUBLIC | ACC_STATIC, params);
    method->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    if (fillerSize != 0U) {
        static_cast<void>(container.GetOrCreateStringItem(std::string(fillerSize, 'x')));
    }
    auto *stringItem = container.GetOrCreateStringItem(text);
    auto *code = container.CreateItem<panda_file::CodeItem>(0U, 0U, std::move(instructions));
    method->SetCode(code);

    container.ComputeLayout();
    const auto stringOffset = stringItem->GetOffset();
    auto *encoded = code->GetInstructions();
    (*encoded)[2U] = static_cast<uint8_t>(stringOffset);
    (*encoded)[3U] = static_cast<uint8_t>(stringOffset >> 8U);
    (*encoded)[4U] = static_cast<uint8_t>(stringOffset >> 16U);
    (*encoded)[5U] = static_cast<uint8_t>(stringOffset >> 24U);
    panda_file::MemoryWriter writer;
    if (!container.Write(&writer)) {
        ADD_FAILURE() << "Failed to serialize direct string ID fixture";
        return {};
    }
    return {writer.GetData(), method->GetOffset(), stringOffset, std::move(text)};
}

uint64_t SerializedStringDataOffset(const GeneratedDirectStringFixture &fixture)
{
    uint64_t cursor = fixture.stringOffset;
    constexpr size_t maxUleB32Size = 5U;
    for (size_t index = 0U; index < maxUleB32Size && cursor < fixture.bytes.size(); ++index) {
        if ((fixture.bytes[cursor++] & 0x80U) == 0U) {
            return cursor;
        }
    }
    ADD_FAILURE() << "Generated string has no bounded ULEB32 header";
    return fixture.bytes.size();
}

void PatchGeneratedString(GeneratedDirectStringFixture *fixture, uint8_t taggedUtf16Length,
                          const std::vector<uint8_t> &mutf8)
{
    ASSERT_NE(fixture, nullptr);
    ASSERT_LT(fixture->stringOffset, fixture->bytes.size());
    ASSERT_EQ(fixture->bytes[fixture->stringOffset] & 0x80U, 0U);
    const auto dataOffset = SerializedStringDataOffset(*fixture);
    ASSERT_LE(dataOffset + mutf8.size(), fixture->bytes.size());
    ASSERT_LT(dataOffset + mutf8.size(), fixture->bytes.size());
    fixture->bytes[fixture->stringOffset] = taggedUtf16Length;
    std::copy(mutf8.begin(), mutf8.end(), fixture->bytes.begin() + static_cast<ptrdiff_t>(dataOffset));
    fixture->bytes[dataOffset + mutf8.size()] = 0U;
}

uint32_t AppendDirectStringBytesAndRetarget(GeneratedDirectStringFixture *fixture,
                                            const std::vector<uint8_t> &serializedString)
{
    if (fixture == nullptr) {
        ADD_FAILURE() << "Cannot append a string to a null fixture";
        return 0U;
    }
    ExecutableTarget target;
    target.methodOffset = fixture->methodOffset;
    uint64_t instructionsOffset = 0U;
    {
        os::mem::ConstBytePtr initialView(reinterpret_cast<std::byte *>(fixture->bytes.data()), fixture->bytes.size(),
                                          [](std::byte *, size_t) noexcept {});
        auto initialFile = panda_file::File::OpenFromMemory(std::move(initialView));
        EXPECT_NE(initialFile, nullptr);
        if (initialFile == nullptr) {
            return 0U;
        }
        instructionsOffset = GetCodeLayout(*initialFile, target).instructionsOffset;
    }
    const auto stringOffset = static_cast<uint32_t>(fixture->bytes.size());
    fixture->bytes.insert(fixture->bytes.end(), serializedString.begin(), serializedString.end());
    WriteUint32(&fixture->bytes, offsetof(panda_file::File::Header, file_size),
                static_cast<uint32_t>(fixture->bytes.size()));
    WriteUint32(&fixture->bytes, instructionsOffset + 2U, stringOffset);
    return stringOffset;
}

void ExpectMalformedMutf8(const std::vector<uint8_t> &encoded, uint8_t taggedUtf16Length,
                          std::string_view expectedDiagnostic)
{
    auto fixture = CreateGeneratedDirectStringFixture(0U, std::string(encoded.size(), 'p'));
    PatchGeneratedString(&fixture, taggedUtf16Length, encoded);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find(expectedDiagnostic), std::string::npos) << result.diagnostic;
}

panda_file::ClassItem *AddPublicEcmascriptClass(panda_file::ItemContainer &container, const char *name)
{
    auto *classItem = container.GetOrCreateClassItem(name);
    classItem->SetAccessFlags(ACC_PUBLIC);
    classItem->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    return classItem;
}

panda_file::MethodItem *AddVoidStaticMethod(panda_file::ItemContainer &container, panda_file::ClassItem *classItem,
                                            const char *methodName)
{
    auto *nameItem = container.GetOrCreateStringItem(methodName);
    auto *returnType = container.GetOrCreatePrimitiveTypeItem(panda_file::Type::TypeId::VOID);
    std::vector<panda_file::MethodParamItem> params;
    auto *prototype = container.GetOrCreateProtoItem(returnType, params);
    auto *method = classItem->AddMethod(nameItem, prototype, ACC_PUBLIC | ACC_STATIC, params);
    method->SetSourceLang(panda_file::SourceLang::ECMASCRIPT);
    return method;
}

panda_file::LiteralArrayItem *AddBoolLiteralArray(panda_file::ItemContainer &container, const std::string &name,
                                                  uint32_t value)
{
    auto *literalArray = container.GetOrCreateLiteralArrayItem(name);
    literalArray->AddItems({
        panda_file::LiteralItem(static_cast<uint8_t>(panda_file::LiteralTag::BOOL)),
        panda_file::LiteralItem(static_cast<uint8_t>(value)),
    });
    return literalArray;
}

GeneratedTypedIdFixture CreateGeneratedTypedIdFixture(bool duplicateLiteralInstruction = false, uint8_t api = 11U,
                                                      uint32_t fillerLiteralCount = 0U)
{
    ScopedItemContainerApi selectedApi(api);
    // defineclasswithbuffer opcode; its index operands are patched below.
    const std::array<uint8_t, 9U> defineClass {0x35, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01};
    const std::array<uint8_t, 5U> callThis {0x2f, 0x07, 0x00, 0x01, 0x02};
    std::vector<uint8_t> instructions(defineClass.begin(), defineClass.end());
    if (duplicateLiteralInstruction) {
        instructions.insert(instructions.end(), defineClass.begin(), defineClass.end());
    }
    instructions.insert(instructions.end(), callThis.begin(), callThis.end());

    panda_file::ItemContainer container;
    auto *classItem = AddPublicEcmascriptClass(container, "LTypedIds;");
    auto *entry = AddVoidStaticMethod(container, classItem, "entry");
    auto *target = AddVoidStaticMethod(container, classItem, "targetMethod");
    for (uint32_t index = 0U; index < fillerLiteralCount; ++index) {
        AddBoolLiteralArray(container, "inventory-filler-" + std::to_string(index), 0U);
    }
    auto *literalArray = AddBoolLiteralArray(container, "typed-id-literal", 1U);
    entry->AddIndexDependency(target);
    entry->AddIndexDependency(literalArray);
    auto *code = container.CreateItem<panda_file::CodeItem>(1U, 2U, std::move(instructions));
    entry->SetCode(code);

    container.ComputeLayout();
    const auto methodIndex = target->GetIndex(entry);
    const auto literalArrayIndex = literalArray->GetIndex(entry);
    EXPECT_LE(std::max(methodIndex, literalArrayIndex), std::numeric_limits<uint16_t>::max());
    auto *encoded = code->GetInstructions();
    const auto patchDefineClass = [encoded, methodIndex, literalArrayIndex](size_t offset) {
        (*encoded)[offset + 2U] = static_cast<uint8_t>(methodIndex);
        (*encoded)[offset + 3U] = static_cast<uint8_t>(methodIndex >> 8U);
        (*encoded)[offset + 4U] = static_cast<uint8_t>(literalArrayIndex);
        (*encoded)[offset + 5U] = static_cast<uint8_t>(literalArrayIndex >> 8U);
    };
    patchDefineClass(0U);
    if (duplicateLiteralInstruction) {
        patchDefineClass(defineClass.size());
    }

    panda_file::MemoryWriter writer;
    if (!container.Write(&writer)) {
        ADD_FAILURE() << "Failed to serialize typed ID fixture";
        return {};
    }
    return {writer.GetData(),
            entry->GetOffset(),
            target->GetOffset(),
            literalArray->GetOffset(),
            static_cast<uint16_t>(methodIndex),
            static_cast<uint16_t>(literalArrayIndex)};
}

struct LiteralPatchLocations {
    uint64_t methodIndexEntryOffset {0U};
    std::optional<uint64_t> inventoryEntryOffset;
};

LiteralPatchLocations GetLiteralPatchLocations(const panda_file::File &file, const GeneratedTypedIdFixture &fixture)
{
    const auto methodIndex =
        file.GetMethodIndex(panda_file::File::EntityId(static_cast<uint32_t>(fixture.entryMethodOffset)));
    EXPECT_LT(fixture.literalArrayIndex, methodIndex.size());
    if (fixture.literalArrayIndex >= methodIndex.size()) {
        return {};
    }
    const auto *entry = reinterpret_cast<const uint8_t *>(methodIndex.data() + fixture.literalArrayIndex);
    const auto inventoryEntryOffset = panda_file::ContainsLiteralArrayInHeader(file.GetHeader()->version)
                                          ? std::optional<uint64_t> {file.GetHeader()->literalarray_idx_off}
                                          : std::nullopt;
    return {file.GetIdFromPointer(entry).GetOffset(), inventoryEntryOffset};
}

uint32_t AppendLiteralAndRetarget(GeneratedTypedIdFixture *fixture, const std::vector<uint8_t> &literalBytes)
{
    if (fixture == nullptr) {
        ADD_FAILURE() << "Cannot append a literal to a null fixture";
        return 0U;
    }
    os::mem::ConstBytePtr initialView(reinterpret_cast<std::byte *>(fixture->bytes.data()), fixture->bytes.size(),
                                      [](std::byte *, size_t) noexcept {});
    auto initialFile = panda_file::File::OpenFromMemory(std::move(initialView));
    EXPECT_NE(initialFile, nullptr);
    if (initialFile == nullptr) {
        return 0U;
    }
    const auto locations = GetLiteralPatchLocations(*initialFile, *fixture);
    const auto appendedOffset = static_cast<uint32_t>(fixture->bytes.size());
    fixture->bytes.insert(fixture->bytes.end(), literalBytes.begin(), literalBytes.end());
    WriteUint32(&fixture->bytes, offsetof(panda_file::File::Header, file_size),
                static_cast<uint32_t>(fixture->bytes.size()));
    WriteUint32(&fixture->bytes, locations.methodIndexEntryOffset, appendedOffset);
    if (locations.inventoryEntryOffset.has_value()) {
        WriteUint32(&fixture->bytes, *locations.inventoryEntryOffset, appendedOffset);
    }
    return appendedOffset;
}

size_t CountCancellationChecks(const std::vector<uint8_t> &literal, uint8_t api, bool duplicate)
{
    auto fixture = CreateGeneratedTypedIdFixture(duplicate, api);
    EXPECT_NE(AppendLiteralAndRetarget(&fixture, literal), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    EXPECT_NE(file, nullptr);
    if (file == nullptr) {
        return size_t {0U};
    }
    EXPECT_EQ(panda_file::ContainsLiteralArrayInHeader(file->GetHeader()->version), api == 11U);
    const auto built = MethodIndex::Build(*file);
    EXPECT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    if (built.index == nullptr) {
        return size_t {0U};
    }
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";
    auto checks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([checks] {
        ++*checks;
        return false;
    });
    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target, {}, cancellation);
    EXPECT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    return *checks;
}

void ExpectRepeatedLiteralPayloadValidatedOnlyOnce(uint8_t api)
{
    constexpr uint32_t valueCount = 64U * 1024U;
    std::vector<uint8_t> literal = EncodeUint32(valueCount);
    literal.reserve(sizeof(uint32_t) + valueCount);
    for (uint32_t index = 0U; index < valueCount; index += 2U) {
        literal.push_back(static_cast<uint8_t>(panda_file::LiteralTag::BOOL));
        literal.push_back(0U);
    }

    const auto singleChecks = CountCancellationChecks(literal, api, false);
    const auto duplicateChecks = CountCancellationChecks(literal, api, true);

    EXPECT_LE(duplicateChecks, singleChecks + 4U) << "api=" << static_cast<uint32_t>(api);
}

std::vector<uint8_t> MakePackedArrayLiteral(panda_file::LiteralTag tag, uint32_t elementWidth, uint32_t elementCount,
                                            bool addOrdinaryPrefix = false)
{
    const uint32_t prefixItems = addOrdinaryPrefix ? 2U : 0U;
    auto literal = EncodeUint32(prefixItems + elementCount + 2U);
    if (addOrdinaryPrefix) {
        literal.push_back(static_cast<uint8_t>(panda_file::LiteralTag::BOOL));
        literal.push_back(1U);
    }
    literal.push_back(static_cast<uint8_t>(tag));
    const auto length = EncodeUint32(elementCount);
    literal.insert(literal.end(), length.begin(), length.end());
    literal.insert(literal.end(), static_cast<size_t>(elementCount) * elementWidth, 0U);
    return literal;
}

TEST(FunctionDisassemblerTypedIdTest, ResolvesMethodAndLiteralArrayInDescriptorOrderAndNamesRegisters)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    ASSERT_EQ(result.disassembly->instructions.size(), 2U);
    const auto &typed = result.disassembly->instructions[0];
    ASSERT_EQ(typed.ids.size(), 2U);
    EXPECT_EQ(typed.ids[0].rawValue, fixture.methodIndex);
    EXPECT_EQ(typed.ids[0].resolutionMode, IdResolutionMode::INDEX);
    EXPECT_EQ(typed.ids[0].kind, IdOperandKind::METHOD);
    EXPECT_EQ(typed.ids[0].resolvedOffset, fixture.targetMethodOffset);
    EXPECT_EQ(typed.ids[0].resolvedText, "targetMethod");
    EXPECT_EQ(typed.ids[1].rawValue, fixture.literalArrayIndex);
    EXPECT_EQ(typed.ids[1].resolutionMode, IdResolutionMode::INDEX);
    EXPECT_EQ(typed.ids[1].kind, IdOperandKind::LITERAL_ARRAY);
    EXPECT_EQ(typed.ids[1].resolvedOffset, fixture.literalArrayOffset);
    EXPECT_TRUE(typed.ids[1].resolvedText.empty());
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(typed),
              "defineclasswithbuffer 1, targetMethod@" + DisassemblyFormatter::FormatPc(fixture.targetMethodOffset) +
                  ", literal-array@" + DisassemblyFormatter::FormatPc(fixture.literalArrayOffset) + ", 2, a0");
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(result.disassembly->instructions[1]), "callthis2 7, v0, a0, a1");
}

TEST(FunctionDisassemblerTypedIdTest, ResolvesLowId32AsDirectOffsetRatherThanIndex)
{
    auto fixture = CreateGeneratedDirectStringFixture(0U, "lowDirect");
    ASSERT_LT(fixture.stringOffset, std::numeric_limits<uint16_t>::max());
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    ASSERT_EQ(result.disassembly->instructions.size(), 1U);
    const auto &instruction = result.disassembly->instructions.front();
    ASSERT_EQ(instruction.ids.size(), 1U);
    EXPECT_EQ(instruction.ids[0].rawValue, fixture.stringOffset);
    EXPECT_EQ(instruction.ids[0].resolutionMode, IdResolutionMode::DIRECT_OFFSET);
    EXPECT_EQ(instruction.ids[0].resolvedOffset, fixture.stringOffset);
    EXPECT_EQ(AsStringView(instruction.ids[0].resolvedText), std::string_view(fixture.text));
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(instruction), "deprecated.getmodulenamespace \"lowDirect\"");
}

TEST(FunctionDisassemblerTypedIdTest, ResolvesId32DirectOffsetAbove64KiB)
{
    auto fixture = CreateGeneratedDirectStringFixture(70U * 1024U, "highDirect");
    ASSERT_GT(fixture.stringOffset, std::numeric_limits<uint16_t>::max());
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    ASSERT_EQ(result.disassembly->instructions.size(), 1U);
    const auto &instruction = result.disassembly->instructions.front();
    ASSERT_EQ(instruction.ids.size(), 1U);
    EXPECT_EQ(instruction.ids[0].rawValue, fixture.stringOffset);
    EXPECT_EQ(instruction.ids[0].resolutionMode, IdResolutionMode::DIRECT_OFFSET);
    EXPECT_EQ(instruction.ids[0].resolvedOffset, fixture.stringOffset);
    EXPECT_EQ(AsStringView(instruction.ids[0].resolvedText), std::string_view(fixture.text));
}

TEST(FunctionDisassemblerTypedIdTest, TreatsRawFfffId32AsAValidDirectOffset)
{
    constexpr uint32_t directOffset = 0xffffU;
    constexpr std::string_view text = "id32ffff";
    auto fixture = CreateGeneratedDirectStringFixture(0U, "original");
    ASSERT_LT(fixture.bytes.size(), directOffset);
    fixture.bytes.resize(directOffset, 0U);
    fixture.bytes.push_back(static_cast<uint8_t>(text.size() << 1U));
    fixture.bytes.insert(fixture.bytes.end(), text.begin(), text.end());
    fixture.bytes.push_back(0U);
    WriteUint32(&fixture.bytes, offsetof(panda_file::File::Header, file_size),
                static_cast<uint32_t>(fixture.bytes.size()));
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";
    uint64_t instructionsOffset = 0U;
    {
        os::mem::ConstBytePtr initialView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                          [](std::byte *, size_t) noexcept {});
        auto initialFile = panda_file::File::OpenFromMemory(std::move(initialView));
        ASSERT_NE(initialFile, nullptr);
        instructionsOffset = GetCodeLayout(*initialFile, target).instructionsOffset;
    }
    ASSERT_LE(instructionsOffset + 5U, fixture.bytes.size());
    WriteUint32(&fixture.bytes, instructionsOffset + 2U, directOffset);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    const auto &id = result.disassembly->instructions.front().ids.front();
    EXPECT_EQ(id.rawValue, directOffset);
    EXPECT_EQ(id.resolutionMode, IdResolutionMode::DIRECT_OFFSET);
    EXPECT_EQ(id.resolvedOffset, directOffset);
    EXPECT_EQ(AsStringView(id.resolvedText), text);
}

TEST(FunctionDisassemblerTypedIdTest, DecodesMutf8ToCanonicalUtf8AndEscapesEmbeddedNul)
{
    const std::vector<uint8_t> mutf8 {
        0xc0U, 0x80U,         // U+0000
        0xc3U, 0xa9U,         // U+00E9
        0xedU, 0xa0U, 0xbdU,  // high surrogate half of the emoji U+1F600
        0xedU, 0xb8U, 0x80U,  // low surrogate U+DE00
    };
    auto fixture = CreateGeneratedDirectStringFixture(0U, std::string(mutf8.size(), 'p'));
    PatchGeneratedString(&fixture, 8U, mutf8);  // four UTF-16 code units, non-ASCII flag
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    const auto &instruction = result.disassembly->instructions.front();
    ASSERT_EQ(instruction.ids.size(), 1U);
    const std::string expected("\0\xC3\xA9\xF0\x9F\x98\x80", 7U);
    EXPECT_EQ(AsStringView(instruction.ids[0].resolvedText), std::string_view(expected));
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(instruction),
              std::string("deprecated.getmodulenamespace \"\\x00\xC3\xA9\xF0\x9F\x98\x80\""));
}

TEST(FunctionDisassemblerTypedIdTest, AcceptsLegalDirectFourByteUtf8)
{
    const std::vector<uint8_t> directUtf8 {0xf0U, 0x9fU, 0x98U, 0x80U};
    auto fixture = CreateGeneratedDirectStringFixture(0U, std::string(directUtf8.size(), 'p'));
    PatchGeneratedString(&fixture, 4U, directUtf8);  // one supplementary code point is two UTF-16 units
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    const std::string expected("\xF0\x9F\x98\x80", 4U);
    EXPECT_EQ(AsStringView(result.disassembly->instructions.front().ids.front().resolvedText),
              std::string_view(expected));
}

TEST(FunctionDisassemblerTypedIdTest, RejectsUnterminatedStringAtEndOfFile)
{
    auto fixture = CreateGeneratedDirectStringFixture(0U, "original");
    ASSERT_NE(AppendDirectStringBytesAndRetarget(&fixture, {2U, 'a'}), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_NE(result.diagnostic.find("not terminated"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsMutf8SequenceTrulyTruncatedAtEndOfFile)
{
    auto fixture = CreateGeneratedDirectStringFixture(0U, "original");
    ASSERT_NE(AppendDirectStringBytesAndRetarget(&fixture, {2U, 0xc2U}), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_NE(result.diagnostic.find("truncated MUTF-8"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsTruncatedMutf8AndUtf16LengthMismatch)
{
    auto truncated = CreateGeneratedDirectStringFixture(0U, "pp");
    PatchGeneratedString(&truncated, 2U, {0xc2U});
    os::mem::ConstBytePtr truncatedView(reinterpret_cast<std::byte *>(truncated.bytes.data()), truncated.bytes.size(),
                                        [](std::byte *, size_t) noexcept {});
    auto truncatedFile = panda_file::File::OpenFromMemory(std::move(truncatedView));
    ASSERT_NE(truncatedFile, nullptr);
    ExecutableTarget target;
    target.methodOffset = truncated.methodOffset;
    target.methodName = "LDirectStringId;.directString";
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*truncatedFile, target).status,
              DisassemblyStatus::MALFORMED_BYTECODE);

    auto mismatch = CreateGeneratedDirectStringFixture(0U, "a");
    PatchGeneratedString(&mismatch, 5U, {'a'});  // claims two UTF-16 code units
    os::mem::ConstBytePtr mismatchView(reinterpret_cast<std::byte *>(mismatch.bytes.data()), mismatch.bytes.size(),
                                       [](std::byte *, size_t) noexcept {});
    auto mismatchFile = panda_file::File::OpenFromMemory(std::move(mismatchView));
    ASSERT_NE(mismatchFile, nullptr);
    target.methodOffset = mismatch.methodOffset;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*mismatchFile, target).status, DisassemblyStatus::MALFORMED_BYTECODE);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsMalformedMutf8EncodingClasses)
{
    struct MalformedMutf8Case {
        std::vector<uint8_t> encoded;
        uint8_t taggedUtf16Length;
        std::string_view diagnostic;
    };
    const std::array<MalformedMutf8Case, 6U> malformed {{
        {{0xc2U, 'A'}, 2U, "invalid MUTF-8"},                      // invalid continuation
        {{0xc1U, 0x81U}, 2U, "overlong MUTF-8"},                   // overlong two-byte sequence
        {{0xedU, 0xb0U, 0x80U}, 2U, "unpaired MUTF-8 surrogate"},  // lone low surrogate
        {{0xedU, 0xa0U, 0x80U, 0xedU, 0xa0U, 0x80U},
         4U,
         "invalid MUTF-8 surrogate pair"},                               // high followed by high surrogate
        {{0xf4U, 0x90U, 0x80U, 0x80U}, 4U, "invalid four-byte MUTF-8"},  // above U+10FFFF
        {{0x80U}, 2U, "invalid MUTF-8"},                                 // continuation byte as a leader
    }};
    for (const auto &testCase : malformed) {
        ASSERT_NO_FATAL_FAILURE(
            ExpectMalformedMutf8(testCase.encoded, testCase.taggedUtf16Length, testCase.diagnostic));
    }
}

TEST(FunctionDisassemblerTypedIdTest, RejectsThreeByteOverlongAndLoneHighSurrogateMutf8)
{
    const std::vector<uint8_t> overlongThreeByte = {0xe0U, 0x80U, 0x80U};
    const std::vector<uint8_t> loneHighSurrogate = {0xedU, 0xa0U, 0x80U};
    ASSERT_NO_FATAL_FAILURE(ExpectMalformedMutf8(overlongThreeByte, 2U, "overlong MUTF-8"));
    ASSERT_NO_FATAL_FAILURE(ExpectMalformedMutf8(loneHighSurrogate, 2U, "invalid MUTF-8"));
}

TEST(FunctionDisassemblerTypedIdTest, CancellationInterruptsLongMutf8Decode)
{
    constexpr size_t longTextSize = 64U * 1024U;
    auto fixture = CreateGeneratedDirectStringFixture(0U, std::string(longTextSize, 'a'));
    const auto dataOffset = SerializedStringDataOffset(fixture);
    ASSERT_LT(dataOffset + longTextSize, fixture.bytes.size());
    fixture.bytes[dataOffset + longTextSize - 1U] = 0xc2U;  // malformed only after a long valid prefix
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    const auto built = MethodIndex::Build(*file);
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";
    auto checks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([checks] { return ++*checks == 6U; });

    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target, {}, cancellation);

    EXPECT_EQ(result.status, DisassemblyStatus::CANCELLED);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_EQ(*checks, 6U);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsLiteralArrayOffsetMissingFromHeaderInventory)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    const auto built = MethodIndex::Build(*file);
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    const auto locations = GetLiteralPatchLocations(*file, fixture);
    ASSERT_TRUE(locations.inventoryEntryOffset.has_value());
    WriteUint32(&fixture.bytes, *locations.inventoryEntryOffset, fixture.literalArrayOffset + 1U);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("literal-array inventory"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, AcceptsLiteralArrayInModernFileWithoutHeaderInventory)
{
    constexpr uint8_t modernApi = 18U;
    auto fixture = CreateGeneratedTypedIdFixture(false, modernApi);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ASSERT_FALSE(panda_file::ContainsLiteralArrayInHeader(file->GetHeader()->version));
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
}

TEST(FunctionDisassemblerTypedIdTest, ValidatesRepeatedLegacyLiteralArrayPayloadOnlyOnce)
{
    ASSERT_NO_FATAL_FAILURE(ExpectRepeatedLiteralPayloadValidatedOnlyOnce(11U));
}

TEST(FunctionDisassemblerTypedIdTest, ValidatesRepeatedModernNoInventoryLiteralArrayPayloadOnlyOnce)
{
    ASSERT_NO_FATAL_FAILURE(ExpectRepeatedLiteralPayloadValidatedOnlyOnce(18U));
}

TEST(FunctionDisassemblerTypedIdTest, RejectsInvalidLiteralArrayTag)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    ASSERT_LE(static_cast<uint64_t>(fixture.literalArrayOffset) + sizeof(uint32_t), fixture.bytes.size());
    fixture.bytes[fixture.literalArrayOffset + sizeof(uint32_t)] = 0xfeU;
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("tag"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsLiteralArrayTruncatedForActualTagWidth)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    std::vector<uint8_t> truncated = EncodeUint32(2U);
    truncated.push_back(static_cast<uint8_t>(panda_file::LiteralTag::DOUBLE));
    truncated.insert(truncated.end(), 3U, 0U);  // four payload bytes satisfy the old count*2 check, not DOUBLE
    const auto appendedOffset = AppendLiteralAndRetarget(&fixture, truncated);
    ASSERT_NE(appendedOffset, 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("truncated"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, AcceptsPackedLiteralArraysForEveryElementWidthAndOddEvenItemCount)
{
    const std::array<std::pair<panda_file::LiteralTag, uint32_t>, 12U> arrayTags {{
        {panda_file::LiteralTag::ARRAY_U1, 1U},
        {panda_file::LiteralTag::ARRAY_U8, 1U},
        {panda_file::LiteralTag::ARRAY_I8, 1U},
        {panda_file::LiteralTag::ARRAY_U16, 2U},
        {panda_file::LiteralTag::ARRAY_I16, 2U},
        {panda_file::LiteralTag::ARRAY_U32, 4U},
        {panda_file::LiteralTag::ARRAY_I32, 4U},
        {panda_file::LiteralTag::ARRAY_F32, 4U},
        {panda_file::LiteralTag::ARRAY_STRING, 4U},
        {panda_file::LiteralTag::ARRAY_U64, 8U},
        {panda_file::LiteralTag::ARRAY_I64, 8U},
        {panda_file::LiteralTag::ARRAY_F64, 8U},
    }};
    for (const auto &[tag, width] : arrayTags) {
        for (const uint32_t length : {1U, 2U}) {
            auto fixture = CreateGeneratedTypedIdFixture();
            ASSERT_NE(AppendLiteralAndRetarget(&fixture, MakePackedArrayLiteral(tag, width, length)), 0U);
            os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                            [](std::byte *, size_t) noexcept {});
            auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
            ASSERT_NE(file, nullptr);
            ExecutableTarget target;
            target.methodOffset = fixture.entryMethodOffset;
            target.methodName = "LTypedIds;.entry";

            const auto result = FunctionDisassembler {}.Disassemble(*file, target);

            EXPECT_EQ(result.status, DisassemblyStatus::SUCCESS)
                << "tag=" << static_cast<uint32_t>(tag) << " length=" << length << " " << result.diagnostic;
        }
    }
}

TEST(FunctionDisassemblerTypedIdTest, AcceptsOrdinaryPairsBeforePackedLiteralArray)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    ASSERT_NE(AppendLiteralAndRetarget(
                  &fixture, MakePackedArrayLiteral(panda_file::LiteralTag::ARRAY_F64, sizeof(uint64_t), 1U, true)),
              0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
}

TEST(FunctionDisassemblerTypedIdTest, RejectsPackedArrayLengthThatDisagreesWithRemainingItems)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    auto literal = MakePackedArrayLiteral(panda_file::LiteralTag::ARRAY_U32, sizeof(uint32_t), 2U);
    WriteUint32(&literal, 0U, 3U);  // tag + length item + one element, but the encoded length says two
    ASSERT_NE(AppendLiteralAndRetarget(&fixture, literal), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("length"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsMaximalPackedArrayLengthWithoutOverflow)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    auto literal = EncodeUint32(2U);  // tag plus the length item
    literal.push_back(static_cast<uint8_t>(panda_file::LiteralTag::ARRAY_U64));
    const auto maximalLength = EncodeUint32(std::numeric_limits<uint32_t>::max());
    literal.insert(literal.end(), maximalLength.begin(), maximalLength.end());
    ASSERT_NE(AppendLiteralAndRetarget(&fixture, literal), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("length"), std::string::npos);
}

TEST(FunctionDisassemblerTypedIdTest, RejectsTruncatedPackedArrayPayloadAtActualElementWidth)
{
    const std::array<std::pair<panda_file::LiteralTag, uint32_t>, 4U> widths {{
        {panda_file::LiteralTag::ARRAY_U8, 1U},
        {panda_file::LiteralTag::ARRAY_U16, 2U},
        {panda_file::LiteralTag::ARRAY_U32, 4U},
        {panda_file::LiteralTag::ARRAY_F64, 8U},
    }};
    for (const auto &[tag, width] : widths) {
        auto fixture = CreateGeneratedTypedIdFixture();
        auto literal = MakePackedArrayLiteral(tag, width, 2U);
        literal.resize(literal.size() - width);
        ASSERT_NE(AppendLiteralAndRetarget(&fixture, literal), 0U);
        os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                        [](std::byte *, size_t) noexcept {});
        auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
        ASSERT_NE(file, nullptr);
        ExecutableTarget target;
        target.methodOffset = fixture.entryMethodOffset;
        target.methodName = "LTypedIds;.entry";

        const auto result = FunctionDisassembler {}.Disassemble(*file, target);

        EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE) << "width=" << width;
        EXPECT_EQ(result.disassembly, nullptr);
        EXPECT_NE(result.diagnostic.find("truncated"), std::string::npos);
    }
}

TEST(FunctionDisassemblerTypedIdTest, RejectsMaliciousLiteralArrayCountWithoutOverflow)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    WriteUint32(&fixture.bytes, fixture.literalArrayOffset, std::numeric_limits<uint32_t>::max());
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
}

TEST(FunctionDisassemblerTypedIdTest, CancellationInterruptsLiteralArrayTagScan)
{
    auto baselineFixture = CreateGeneratedTypedIdFixture();
    os::mem::ConstBytePtr baselineView(reinterpret_cast<std::byte *>(baselineFixture.bytes.data()),
                                       baselineFixture.bytes.size(), [](std::byte *, size_t) noexcept {});
    auto baselineFile = panda_file::File::OpenFromMemory(std::move(baselineView));
    ASSERT_NE(baselineFile, nullptr);
    ExecutableTarget target;
    target.methodOffset = baselineFixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";
    const auto baseline = FunctionDisassembler {}.Disassemble(*baselineFile, target);
    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    ASSERT_FALSE(baseline.disassembly->instructions.empty());
    DisassemblyLimits limits;
    limits.maxFormattedTextBytes =
        DisassemblyFormatter::FormatInstruction(baseline.disassembly->instructions.front()).size() - 1U;

    constexpr uint32_t valueCount = 64U * 1024U;
    std::vector<uint8_t> literal = EncodeUint32(valueCount);
    literal.reserve(sizeof(uint32_t) + static_cast<size_t>(valueCount) * 2U);
    for (uint32_t index = 0U; index < valueCount; index += 2U) {
        literal.push_back(index + 2U == valueCount ? 0xfeU : static_cast<uint8_t>(panda_file::LiteralTag::BOOL));
        literal.push_back(0U);
    }
    literal.insert(literal.end(), valueCount, 0U);  // keep the old count*2 lower-bound check satisfied
    auto fixture = CreateGeneratedTypedIdFixture();
    ASSERT_NE(AppendLiteralAndRetarget(&fixture, literal), 0U);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    const auto built = MethodIndex::Build(*file);
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    target.methodOffset = fixture.entryMethodOffset;
    auto checks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([checks] { return ++*checks == 7U; });

    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target, limits, cancellation);

    EXPECT_EQ(result.status, DisassemblyStatus::CANCELLED);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_EQ(*checks, 7U);
}

TEST(FunctionDisassemblerTypedIdTest, EscapesResolvedStringsAndAccountsForBytesIdsAndText)
{
    const std::string resolvedText = "quoted\"\\\n\t\r\x01";
    const std::vector<uint8_t> placeholder {0xfeU, 0x09U, 0x00U, 0x00U};
    panda_file::ItemContainer container;
    auto *classItem = AddPublicEcmascriptClass(container, "LEscapedId;");
    auto *method = AddVoidStaticMethod(container, classItem, "escaped");
    auto *stringItem = container.GetOrCreateStringItem(resolvedText);
    method->AddIndexDependency(stringItem);
    auto *code = container.CreateItem<panda_file::CodeItem>(0U, 0U, placeholder);
    method->SetCode(code);
    container.ComputeLayout();
    const auto stringIndex = stringItem->GetIndex(method);
    ASSERT_LE(stringIndex, std::numeric_limits<uint16_t>::max());
    (*code->GetInstructions())[2U] = static_cast<uint8_t>(stringIndex);
    (*code->GetInstructions())[3U] = static_cast<uint8_t>(stringIndex >> 8U);
    panda_file::MemoryWriter writer;
    ASSERT_TRUE(container.Write(&writer));
    auto bytes = writer.GetData();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = method->GetOffset();
    target.methodName = "LEscapedId;.escaped";

    const auto baseline = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    ASSERT_EQ(baseline.disassembly->instructions.size(), 1U);
    const auto &instruction = baseline.disassembly->instructions[0];
    ASSERT_EQ(instruction.ids.size(), 1U);
    EXPECT_EQ(AsStringView(instruction.ids[0].resolvedText), std::string_view(resolvedText));
    EXPECT_EQ(DisassemblyFormatter::FormatInstruction(instruction),
              "throw.undefinedifholewithname \"quoted\\\"\\\\\\n\\t\\r\\x01\"");

    const auto formattedBytes = DisassemblyFormatter::FormatInstruction(instruction).size();
    DisassemblyLimits limits;
    limits.maxFormattedTextBytes = formattedBytes - 1U;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*file, target, limits).status,
              DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    limits.maxFormattedTextBytes = formattedBytes;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*file, target, limits).status, DisassemblyStatus::SUCCESS);

    const auto modelBytes = FunctionDisassembler::EstimateModelBytes(*baseline.disassembly);
    ASSERT_NE(baseline.disassembly->OwnedResource(), nullptr);
    EXPECT_EQ(modelBytes, sizeof(FunctionDisassembly) + baseline.disassembly->OwnedResource()->LiveBytes());
    limits = {};
    limits.maxModelBytes = modelBytes - 1U;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*file, target, limits).status,
              DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
}

TEST(FunctionDisassemblerTypedIdTest, ModelLimitAccountsForSimultaneousFormattedIdPeak)
{
    auto fixture = CreateGeneratedDirectStringFixture(0U, std::string(128U, 'q'));
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LDirectStringId;.directString";
    const auto baseline = FunctionDisassembler {}.Disassemble(*file, target);
    ASSERT_EQ(baseline.status, DisassemblyStatus::SUCCESS) << baseline.diagnostic;
    ASSERT_NE(baseline.disassembly, nullptr);
    const auto retainedBytes = FunctionDisassembler::EstimateModelBytes(*baseline.disassembly);
    const auto peakBytes = FunctionDisassembler::EstimatePeakModelBytes(*baseline.disassembly);

    ASSERT_GT(peakBytes, retainedBytes);
    DisassemblyLimits limits;
    limits.maxModelBytes = peakBytes - 1U;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*file, target, limits).status,
              DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED);
    limits.maxModelBytes = peakBytes;
    EXPECT_EQ(FunctionDisassembler {}.Disassemble(*file, target, limits).status, DisassemblyStatus::SUCCESS);
}

TEST(FunctionDisassemblerTypedIdTest, UsesOneOwnedResourceForEveryNestedModelAllocation)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    auto *resource = result.disassembly->Resource();
    ASSERT_NE(result.disassembly->OwnedResource(), nullptr);
    EXPECT_EQ(resource, result.disassembly->OwnedResource());
    EXPECT_EQ(result.disassembly->instructions.get_allocator().resource(), resource);
    ASSERT_FALSE(result.disassembly->instructions.empty());
    for (const auto &instruction : result.disassembly->instructions) {
        EXPECT_EQ(instruction.mnemonic.get_allocator().resource(), resource);
        EXPECT_EQ(instruction.operands.get_allocator().resource(), resource);
        EXPECT_EQ(instruction.bytes.get_allocator().resource(), resource);
        EXPECT_EQ(instruction.registers.get_allocator().resource(), resource);
        EXPECT_EQ(instruction.immediates.get_allocator().resource(), resource);
        EXPECT_EQ(instruction.ids.get_allocator().resource(), resource);
        for (const auto &id : instruction.ids) {
            EXPECT_EQ(id.resolvedText.get_allocator().resource(), resource);
        }
    }
}

TEST(FunctionDisassemblerTypedIdTest, RejectsReservedId16InGeneratedFixture)
{
    auto fixture = CreateGeneratedTypedIdFixture();
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";
    uint64_t instructionsOffset = 0U;
    {
        os::mem::ConstBytePtr initialView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                          [](std::byte *, size_t) noexcept {});
        auto initialFile = panda_file::File::OpenFromMemory(std::move(initialView));
        ASSERT_NE(initialFile, nullptr);
        instructionsOffset = GetCodeLayout(*initialFile, target).instructionsOffset;
    }
    ASSERT_LE(instructionsOffset + 3U, fixture.bytes.size());
    fixture.bytes[instructionsOffset + 2U] = 0xffU;
    fixture.bytes[instructionsOffset + 3U] = 0xffU;
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    EXPECT_EQ(result.status, DisassemblyStatus::MALFORMED_BYTECODE);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_NE(result.diagnostic.find("reserved 16-bit index"), std::string::npos);
}

void AuditIdFlags(const std::array<uint8_t, 64U> &encoded, size_t &recognizedIds, size_t &unknownIds)
{
    BytecodeInstructionSafe instruction(encoded.data(), encoded.data(), encoded.data() + encoded.size() - 1U);
    if (!instruction.IsPrimaryOpcodeValid() || !instruction.IsOpcodeValid()) {
        return;
    }
    for (size_t operandIndex = 0U; operandIndex < instruction.GetOperandCount(); ++operandIndex) {
        const auto descriptor = instruction.GetOperandDescriptor(operandIndex);
        if (descriptor.kind != BytecodeOperandKind::ID) {
            continue;
        }
        const bool isString =
            instruction.IsIdMatchFlag(descriptor.typedIndex, BytecodeInstructionSafe::Flags::STRING_ID);
        const bool isMethod =
            instruction.IsIdMatchFlag(descriptor.typedIndex, BytecodeInstructionSafe::Flags::METHOD_ID);
        const bool isLiteralArray =
            instruction.IsIdMatchFlag(descriptor.typedIndex, BytecodeInstructionSafe::Flags::LITERALARRAY_ID);
        const auto kindCount =
            static_cast<uint32_t>(isString) + static_cast<uint32_t>(isMethod) + static_cast<uint32_t>(isLiteralArray);
        EXPECT_LE(kindCount, 1U) << "opcode=" << static_cast<uint32_t>(instruction.GetOpcode())
                                 << " id=" << descriptor.typedIndex;
        if (kindCount == 0U) {
            ++unknownIds;
        } else {
            ++recognizedIds;
        }
    }
}

TEST(FunctionDisassemblerTypedIdTest, AuditsGeneratedIdFlagsForUnknownAndConflictingKinds)
{
    std::array<uint8_t, 64U> encoded {};
    size_t recognizedIds = 0U;
    size_t unknownIds = 0U;

    for (uint32_t primary = 0U; primary <= std::numeric_limits<uint8_t>::max(); ++primary) {
        encoded.fill(0U);
        encoded[0] = static_cast<uint8_t>(primary);
        BytecodeInstructionSafe primaryInstruction(encoded.data(), encoded.data(),
                                                   encoded.data() + encoded.size() - 1U);
        if (!primaryInstruction.IsPrimaryOpcodeValid()) {
            continue;
        }
        if (!primaryInstruction.IsPrefixed()) {
            AuditIdFlags(encoded, recognizedIds, unknownIds);
            continue;
        }
        for (uint32_t secondary = 0U; secondary <= std::numeric_limits<uint8_t>::max(); ++secondary) {
            encoded[1] = static_cast<uint8_t>(secondary);
            AuditIdFlags(encoded, recognizedIds, unknownIds);
        }
    }

    EXPECT_GT(recognizedIds, 0U);
    EXPECT_EQ(unknownIds, 0U);
}

TEST(FunctionDisassemblerTypedIdTest, CancellationInterruptsLongMultiHeaderIndexScan)
{
    constexpr uint32_t indexHeaderCount = 1024U;
    auto fixture = CreateGeneratedTypedIdFixture();
    MethodIndexResult built;
    {
        os::mem::ConstBytePtr initialView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                          [](std::byte *, size_t) noexcept {});
        auto initialFile = panda_file::File::OpenFromMemory(std::move(initialView));
        ASSERT_NE(initialFile, nullptr);
        built = MethodIndex::Build(*initialFile);
        ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
        ASSERT_NE(built.index, nullptr);
    }
    const auto indexSectionOffset = static_cast<uint32_t>(fixture.bytes.size());
    fixture.bytes.resize(fixture.bytes.size() +
                         static_cast<size_t>(indexHeaderCount) * sizeof(panda_file::File::IndexHeader));
    for (uint32_t index = 0U; index < indexHeaderCount; ++index) {
        const uint64_t headerOffset = static_cast<uint64_t>(indexSectionOffset) +
                                      static_cast<uint64_t>(index) * sizeof(panda_file::File::IndexHeader);
        WriteUint32(&fixture.bytes, headerOffset + offsetof(panda_file::File::IndexHeader, start), 0U);
        WriteUint32(&fixture.bytes, headerOffset + offsetof(panda_file::File::IndexHeader, end), 1U);
    }
    WriteUint32(&fixture.bytes, offsetof(panda_file::File::Header, file_size),
                static_cast<uint32_t>(fixture.bytes.size()));
    WriteUint32(&fixture.bytes, offsetof(panda_file::File::Header, num_indexes), indexHeaderCount);
    WriteUint32(&fixture.bytes, offsetof(panda_file::File::Header, index_section_off), indexSectionOffset);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(file->GetHeader()->num_indexes, indexHeaderCount);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";
    auto checks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([checks] { return ++*checks == 10U; });

    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target, {}, cancellation);

    EXPECT_EQ(result.status, DisassemblyStatus::CANCELLED);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_EQ(*checks, 10U);
}

TEST(FunctionDisassemblerTypedIdTest, CancellationInterruptsLongLiteralArrayInventoryBuild)
{
    constexpr uint32_t fillerLiteralCount = 1024U;
    auto fixture = CreateGeneratedTypedIdFixture(false, 11U, fillerLiteralCount);
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ASSERT_GT(file->GetHeader()->num_literalarrays, fillerLiteralCount);
    const auto built = MethodIndex::Build(*file);
    ASSERT_EQ(built.status, MethodIndexStatus::SUCCESS) << built.diagnostic;
    ASSERT_NE(built.index, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.entryMethodOffset;
    target.methodName = "LTypedIds;.entry";
    auto checks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([checks] { return ++*checks == 6U; });

    const auto result = FunctionDisassembler {}.Disassemble(*file, *built.index, target, {}, cancellation);

    EXPECT_EQ(result.status, DisassemblyStatus::CANCELLED);
    EXPECT_EQ(result.disassembly, nullptr);
    EXPECT_EQ(*checks, 6U);
}

TEST(FunctionDisassemblerControlFlowTest, RetainsTypedControlFlowKinds)
{
    auto fixture = CreateGeneratedControlFlowFixture();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);
    ExecutableTarget target;
    target.methodOffset = fixture.methodOffset;
    target.methodName = "LControlFlow;.controlFlow";

    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    ASSERT_EQ(result.disassembly->instructions.size(), 4U);
    EXPECT_EQ(result.disassembly->instructions[0].flowKind, ControlFlowKind::CONDITIONAL_JUMP);
    EXPECT_EQ(result.disassembly->instructions[0].jumpTarget, 4U);
    EXPECT_EQ(result.disassembly->instructions[1].flowKind, ControlFlowKind::UNCONDITIONAL_JUMP);
    EXPECT_EQ(result.disassembly->instructions[1].jumpTarget, 4U);
    EXPECT_EQ(result.disassembly->instructions[2].flowKind, ControlFlowKind::RETURN);
    EXPECT_EQ(result.disassembly->instructions[3].flowKind, ControlFlowKind::THROW);
}

TEST(FunctionDisassemblerControlFlowTest, ConditionalThrowsRetainNormalAndCatchSuccessors)
{
    // Real encoded dynamic instructions; unlike plain throw they can also complete normally.
    const std::vector<std::vector<uint8_t>> cases {
        {0xfeU, 0x07U, 0U},      // throw.ifsupernotcorrectcall imm8
        {0xfeU, 0x08U, 0U, 0U},  // throw.ifsupernotcorrectcall imm16
        {0xfeU, 0x05U, 0U},      // throw.ifnotobject v0
        {0xfeU, 0x06U, 0U, 0U},  // throw.undefinedifhole v0, v0
    };
    for (auto instructions : cases) {
        const auto protectedSize = static_cast<uint32_t>(instructions.size());
        SCOPED_TRACE(static_cast<uint32_t>(instructions[1]));
        instructions.insert(instructions.end(), {0x65U, 0xfeU, 0x00U});  // returnundefined; handler: throw
        auto fixture = CreateGeneratedControlFlowFixture(std::move(instructions), protectedSize);
        os::mem::ConstBytePtr view(reinterpret_cast<std::byte *>(fixture.bytes.data()), fixture.bytes.size(),
                                   [](std::byte *, size_t) noexcept {});
        auto file = panda_file::File::OpenFromMemory(std::move(view));
        ASSERT_NE(file, nullptr);
        ExecutableTarget target;
        target.methodOffset = fixture.methodOffset;
        const auto decoded = FunctionDisassembler {}.Disassemble(*file, target);
        ASSERT_EQ(decoded.status, DisassemblyStatus::SUCCESS) << decoded.diagnostic;
        ASSERT_EQ(decoded.disassembly->instructions.size(), 3U);
        EXPECT_TRUE(decoded.disassembly->instructions[0].canThrow);
        EXPECT_FALSE(decoded.disassembly->instructions[0].isTerminator);
        EXPECT_EQ(decoded.disassembly->instructions[0].flowKind, ControlFlowKind::NORMAL);
        EXPECT_FALSE(decoded.disassembly->instructions[1].canThrow);
        ASSERT_EQ(decoded.disassembly->tryRegions.size(), 1U);
        const auto built = ControlFlowGraphBuilder {}.Build(decoded.disassembly);
        ASSERT_EQ(built.status, CfgStatus::SUCCESS) << built.diagnostic;
        ASSERT_EQ(built.graph->blocks.size(), 3U);
        EXPECT_TRUE(std::any_of(built.graph->edges.begin(), built.graph->edges.end(), [](const auto &edge) {
            return edge.source == 0U && edge.target == 1U && edge.kind == CfgEdgeKind::FALLTHROUGH;
        }));
        EXPECT_TRUE(std::any_of(built.graph->edges.begin(), built.graph->edges.end(), [](const auto &edge) {
            return edge.source == 0U && edge.target == 2U && edge.kind == CfgEdgeKind::EXCEPTION;
        }));
        const auto analyzed = CfgAnalyzer {}.Analyze(built.graph);
        ASSERT_EQ(analyzed.status, CfgAnalysisStatus::SUCCESS) << analyzed.diagnostic;
        EXPECT_TRUE(analyzed.analysis->blockInfo[2U].reachable);
    }
}

TEST(FunctionDisassemblerTypedImmediateTest, PreservesUnsignedAndFloatingImmediateValues)
{
    constexpr uint64_t highBitU8 = 0x8eU;
    constexpr uint64_t piRawBits = 0x40091eb851eb851fULL;
    const std::vector<uint8_t> instructions {
        0x2f, 0x8e, 0x00, 0x02, 0x03,                          // callthis2 142, v0, v2, v3
        0x63, 0x1f, 0x85, 0xeb, 0x51, 0xb8, 0x1e, 0x09, 0x40,  // fldai 3.14
    };

    panda_file::ItemContainer container;
    auto *classItem = AddPublicEcmascriptClass(container, "LTypedImmediate;");
    auto *method = AddVoidStaticMethod(container, classItem, "typedImmediate");
    auto *code = container.CreateItem<panda_file::CodeItem>(4U, 0U, instructions);
    method->SetCode(code);

    panda_file::MemoryWriter writer;
    ASSERT_TRUE(container.Write(&writer));
    auto bytes = writer.GetData();
    os::mem::ConstBytePtr bytesView(reinterpret_cast<std::byte *>(bytes.data()), bytes.size(),
                                    [](std::byte *, size_t) noexcept {});
    auto file = panda_file::File::OpenFromMemory(std::move(bytesView));
    ASSERT_NE(file, nullptr);

    ExecutableTarget target;
    target.methodOffset = method->GetOffset();
    target.methodName = "LTypedImmediate;.typedImmediate";
    const auto result = FunctionDisassembler {}.Disassemble(*file, target);

    ASSERT_EQ(result.status, DisassemblyStatus::SUCCESS) << result.diagnostic;
    ASSERT_NE(result.disassembly, nullptr);
    ASSERT_EQ(result.disassembly->instructions.size(), 2U);

    const auto &unsignedImmediate = result.disassembly->instructions[0].immediates;
    ASSERT_EQ(unsignedImmediate.size(), 1U);
    EXPECT_EQ(unsignedImmediate[0].kind, ImmediateKind::UNSIGNED);
    EXPECT_EQ(unsignedImmediate[0].bitWidth, 8U);
    EXPECT_EQ(unsignedImmediate[0].rawBits, highBitU8);
    ASSERT_TRUE(std::holds_alternative<uint64_t>(unsignedImmediate[0].value));
    EXPECT_EQ(std::get<uint64_t>(unsignedImmediate[0].value), 142U);

    const auto &floatingImmediate = result.disassembly->instructions[1].immediates;
    ASSERT_EQ(floatingImmediate.size(), 1U);
    EXPECT_EQ(floatingImmediate[0].kind, ImmediateKind::FLOAT);
    EXPECT_EQ(floatingImmediate[0].bitWidth, 64U);
    EXPECT_EQ(floatingImmediate[0].rawBits, piRawBits);
    ASSERT_TRUE(std::holds_alternative<double>(floatingImmediate[0].value));
    EXPECT_DOUBLE_EQ(std::get<double>(floatingImmediate[0].value), 3.14);

    EXPECT_NE(DisassemblyFormatter::FormatTooltip(result.disassembly->instructions[0]).find("u8 142 (raw 0x8E)"),
              std::string::npos);
    EXPECT_NE(DisassemblyFormatter::FormatTooltip(result.disassembly->instructions[1])
                  .find("f64 3.14 (raw 0x40091EB851EB851F)"),
              std::string::npos);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
