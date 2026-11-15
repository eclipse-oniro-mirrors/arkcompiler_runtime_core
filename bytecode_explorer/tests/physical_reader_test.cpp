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

#include "core/document/abc_document.h"
#include "core/reader/physical_reader.h"
#include "core/reader/dynamic_structure_reader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <unistd.h>

namespace panda::bytecode_explorer::test {
namespace {

struct TestAbcHeader {
    std::array<uint8_t, 8U> magic;
    uint32_t checksum;
    std::array<uint8_t, 4U> version;
    uint32_t fileSize;
    uint32_t foreignOffset;
    uint32_t foreignSize;
    uint32_t classCount;
    uint32_t classIndexOffset;
    uint32_t lineNumberProgramCount;
    uint32_t lineNumberProgramIndexOffset;
    uint32_t literalArrayCount;
    uint32_t literalArrayIndexOffset;
    uint32_t indexSectionCount;
    uint32_t indexSectionOffset;
};

static_assert(sizeof(TestAbcHeader) == 60U);

// Reads a trivially copyable value from raw bytes without risky memory functions.
template <typename T>
T ReadPod(const char *bytes)
{
    T value {};
    std::copy(bytes, bytes + sizeof(T), reinterpret_cast<char *>(&value));
    return value;
}

// Writes a trivially copyable value into raw bytes without risky memory functions.
template <typename T>
void WritePod(char *bytes, const T &value)
{
    const auto *first = reinterpret_cast<const char *>(&value);
    std::copy(first, first + sizeof(T), bytes);
}

PhysicalProgram ReadProgram(const std::string &path)
{
    auto document = AbcDocument::Open(path);
    EXPECT_TRUE(document.HasValue()) << document.Error().Message();
    if (!document.HasValue()) {
        return {};
    }
    auto result = PhysicalReader::Read(document.Value()->File());
    EXPECT_TRUE(result.HasValue()) << result.Error().Message();
    return result.HasValue() ? std::move(result.Value()) : PhysicalProgram {};
}

const PhysicalRecord *FindModule(const PhysicalProgram &program, const std::string &name)
{
    const auto found = std::find_if(program.records.begin(), program.records.end(),
                                    [name](const auto &record) { return record.isModule && record.name == name; });
    return found == program.records.end() ? nullptr : &*found;
}

bool HasMethod(const PhysicalRecord &record, const std::string &name)
{
    return std::any_of(record.methods.begin(), record.methods.end(),
                       [name](const auto &method) { return method.name == name; });
}

std::string DescribeRecords(const PhysicalProgram &program)
{
    std::ostringstream output;
    for (const auto &record : program.records) {
        output << record.name << " [module=" << record.isModule << ", fields=" << record.fields.size()
               << ", methods=" << record.methods.size() << "]\n";
        for (const auto &field : record.fields) {
            output << "  " << field.name;
            if (field.unsignedValue.has_value()) {
                output << "=" << *field.unsignedValue;
            }
            output << '\n';
        }
    }
    return output.str();
}

enum class CorruptionKind : uint8_t {
    CLASS_OFFSET,
    LITERAL_TAG,
    BYTECODE_OPCODE,
};

// One corruption request: the bytes under test, the header they were parsed from, and the corruption site.
struct CorruptionRequest {
    std::vector<char> *bytes;
    const TestAbcHeader *header;
    CorruptionKind kind;
    std::optional<uint64_t> bytecodeOffset;
};

bool ApplyCorruption(const CorruptionRequest &request)
{
    auto &bytes = *request.bytes;
    const auto &header = *request.header;
    const auto kind = request.kind;
    const auto &bytecodeOffset = request.bytecodeOffset;
    if (kind == CorruptionKind::CLASS_OFFSET) {
        EXPECT_GT(header.classCount, 0U);
        EXPECT_LE(static_cast<uint64_t>(header.classIndexOffset) + sizeof(uint32_t), bytes.size());
        if (header.classCount == 0U ||
            static_cast<uint64_t>(header.classIndexOffset) + sizeof(uint32_t) > bytes.size()) {
            return false;
        }
        WritePod(bytes.data() + header.classIndexOffset, header.fileSize);
    } else if (kind == CorruptionKind::LITERAL_TAG) {
        EXPECT_GT(header.literalArrayCount, 0U);
        EXPECT_LE(static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t), bytes.size());
        if (header.literalArrayCount == 0U ||
            static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t) > bytes.size()) {
            return false;
        }
        uint32_t literalOffset = 0U;
        literalOffset = ReadPod<uint32_t>(bytes.data() + header.literalArrayIndexOffset);
        constexpr uint64_t lengthSize = sizeof(uint32_t);
        EXPECT_LT(static_cast<uint64_t>(literalOffset) + lengthSize, bytes.size());
        if (static_cast<uint64_t>(literalOffset) + lengthSize >= bytes.size()) {
            return false;
        }
        bytes[literalOffset + lengthSize] = static_cast<char>(0xFE);
    } else {
        EXPECT_TRUE(bytecodeOffset.has_value());
        if (!bytecodeOffset.has_value() || *bytecodeOffset + 1U >= bytes.size()) {
            return false;
        }
        EXPECT_LT(*bytecodeOffset + 1U, bytes.size());
        // THROW is a legal primary prefix, while 0xFF is outside its
        // secondary-opcode range. This used to reach GetFormat() and abort.
        bytes[*bytecodeOffset] = static_cast<char>(0xFE);
        bytes[*bytecodeOffset + 1U] = static_cast<char>(0xFF);
    }
    return true;
}

class TemporaryCorruptAbc final {
public:
    explicit TemporaryCorruptAbc(const std::string &sourcePath, CorruptionKind kind = CorruptionKind::CLASS_OFFSET,
                                 std::optional<uint64_t> bytecodeOffset = std::nullopt)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        std::vector<char> bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_GE(bytes.size(), sizeof(TestAbcHeader));
        if (bytes.size() < sizeof(TestAbcHeader)) {
            return;
        }
        TestAbcHeader header {};
        header = ReadPod<TestAbcHeader>(bytes.data());
        const CorruptionRequest request {&bytes, &header, kind, bytecodeOffset};
        if (!ApplyCorruption(request)) {
            return;
        }

        char path[] = "/tmp/ark_bytecode_explorer_corrupt_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryCorruptAbc()
    {
        if (!path_.empty()) {
            EXPECT_EQ(unlink(path_.c_str()), 0);
        }
    }

    const std::string &Path() const
    {
        return path_;
    }

private:
    std::string path_;
};

TEST(PhysicalReaderTest, NormalizesNonMergeModuleAndGlobalMethods)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
    const auto *module = FindModule(program, "structure_alpha.ts");
    ASSERT_NE(module, nullptr) << DescribeRecords(program);
    EXPECT_FALSE(module->scopeNames.empty());
    EXPECT_TRUE(HasMethod(*module, "#*#alphaEntry"));
    EXPECT_TRUE(HasMethod(*module, "#~@2=#Counter"));
    EXPECT_FALSE(module->moduleBindings.empty());
}

TEST(PhysicalReaderTest, ReportsMalformedEntityOffsetsWithoutTerminating)
{
    TemporaryCorruptAbc corrupt(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    auto document = AbcDocument::Open(corrupt.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto result = PhysicalReader::Read(document.Value()->File());
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::BACKEND_ERROR);
}

TEST(PhysicalReaderTest, ReportsUnknownLiteralTagsWithoutTerminating)
{
    TemporaryCorruptAbc corrupt(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC, CorruptionKind::LITERAL_TAG);
    auto document = AbcDocument::Open(corrupt.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto result = PhysicalReader::Read(document.Value()->File());
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::BACKEND_ERROR);
}

TEST(PhysicalReaderTest, ReportsInvalidPrefixedBytecodeWithoutTerminating)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    std::optional<uint64_t> instructionOffset;
    for (const auto &record : program.records) {
        for (const auto &method : record.methods) {
            const auto found = std::find_if(method.evidence.begin(), method.evidence.end(),
                                            [](const auto &evidence) { return evidence.range.size >= 2U; });
            if (found != method.evidence.end()) {
                instructionOffset = found->range.offset;
                break;
            }
        }
        if (instructionOffset.has_value()) {
            break;
        }
    }
    ASSERT_TRUE(instructionOffset.has_value());

    TemporaryCorruptAbc corrupt(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, CorruptionKind::BYTECODE_OPCODE,
                                instructionOffset);
    auto document = AbcDocument::Open(corrupt.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto result = PhysicalReader::Read(document.Value()->File());
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::BACKEND_ERROR);
}

TEST(PhysicalReaderTest, KeepsModuleWithoutOptionalScopeNames)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    const auto *module = FindModule(program, "structure_beta.ts");
    ASSERT_NE(module, nullptr) << DescribeRecords(program);
    EXPECT_TRUE(module->scopeNames.empty());
    EXPECT_TRUE(HasMethod(*module, "func_main_0"));
    EXPECT_TRUE(HasMethod(*module, "betaHelper"));
    EXPECT_TRUE(HasMethod(*module, "BetaModel"));
    EXPECT_EQ(module->moduleBindings.size(), 3U);
}

TEST(PhysicalReaderTest, KeepsMergedModuleWithoutOptionalScopeNames)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_MERGED_NO_SCOPE_TEST_ABC);
    const auto *module = FindModule(program, "structure_beta");
    ASSERT_NE(module, nullptr) << DescribeRecords(program);
    EXPECT_TRUE(module->scopeNames.empty());
    EXPECT_TRUE(HasMethod(*module, "func_main_0"));
    EXPECT_FALSE(module->moduleBindings.empty());
}

TEST(PhysicalReaderTest, SnapshotsClassBufferAndAccessorMethodReferences)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    const auto found = std::find_if(program.literalArrays.begin(), program.literalArrays.end(),
                                    [](const auto &literal) { return !literal.methods.empty(); });
    ASSERT_NE(found, program.literalArrays.end());
    EXPECT_TRUE(std::all_of(found->methods.begin(), found->methods.end(),
                            [](const auto &method) { return method.name.empty(); }));
    EXPECT_TRUE(
        std::any_of(found->methods.begin(), found->methods.end(), [](const auto &method) { return !method.isStatic; }));
    EXPECT_TRUE(
        std::any_of(found->methods.begin(), found->methods.end(), [](const auto &method) { return method.isStatic; }));
    const auto *module = FindModule(program, "structure_beta.ts");
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(module->classDefinitions.size(), 1U);
    EXPECT_TRUE(std::any_of(module->classDefinitions.front().members.begin(),
                            module->classDefinitions.front().members.end(), [](const auto &method) {
                                return method.name == "summary" && method.kind == PhysicalLiteralMethodKind::GETTER;
                            }));
}

TEST(PhysicalReaderTest, RetainsOnlyCompactSemanticBytecodeEvidence)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    size_t methodCount = 0;
    size_t evidenceCount = 0;
    for (const auto &record : program.records) {
        methodCount += record.methods.size();
        for (const auto &method : record.methods) {
            evidenceCount += method.evidence.size();
            if (method.name.find("value") != std::string::npos) {
                EXPECT_GT(method.numArgs, 0U);
            }
        }
    }
    EXPECT_GT(methodCount, 10U);
    EXPECT_GT(evidenceCount, 0U);
    EXPECT_LT(evidenceCount, methodCount * 20U);
}

const PhysicalAnnotation *FindRouteAnnotation(const PhysicalProgram &program)
{
    for (const auto &record : program.records) {
        for (const auto &method : record.methods) {
            const auto found = std::find_if(method.annotations.begin(), method.annotations.end(),
                                            [](const auto &annotation) { return annotation.name == "Route"; });
            if (found != method.annotations.end()) {
                return &*found;
            }
        }
    }
    return nullptr;
}

TEST(PhysicalReaderTest, CopiesAnnotationElementNamesTagsAndValues)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    const auto *route = FindRouteAnnotation(program);
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->elements.size(), 1U);
    EXPECT_EQ(route->elements.front().name, "path");
    EXPECT_EQ(route->elements.front().tag, 'C');
    EXPECT_EQ(route->elements.front().displayValue, "/counter");
    EXPECT_TRUE(route->elements.front().decoded);
}

TEST(PhysicalReaderTest, InventoriesLiteralArraysWithoutFabricatingTheirExtent)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_FALSE(program.literalArrays.empty());
    for (const auto &literal : program.literalArrays) {
        EXPECT_GT(literal.valueCount, 0U);
        EXPECT_EQ(literal.range.size, 0U);
        EXPECT_LT(literal.range.offset, program.fileSize);
    }
}

TEST(PhysicalReaderTest, InventoriesEveryNestedLiteralArrayReference)
{
    const auto program = ReadProgram(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    std::set<uint32_t> inventory;
    for (const auto &literal : program.literalArrays) {
        inventory.emplace(static_cast<uint32_t>(literal.range.offset));
    }
    for (const auto &literal : program.literalArrays) {
        for (const auto nestedOffset : literal.nestedArrayOffsets) {
            EXPECT_NE(inventory.find(nestedOffset), inventory.end());
        }
    }
}

TEST(PhysicalReaderTest, RetainsOnlyUniqueRequiredStringInventoryCapacity)
{
    const std::array<const char *, 4U> paths {
        BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC,
        BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC, BYTECODE_EXPLORER_STRUCTURE_MERGED_NO_SCOPE_TEST_ABC};
    bool foundDuplicateHeavyFixture = false;
    for (const auto *path : paths) {
        const auto program = ReadProgram(path);
        const auto requiredReferenceCount = static_cast<size_t>(std::count_if(
            program.metadataReferences.begin(), program.metadataReferences.end(), [](const auto &reference) {
                return reference.targetKind == PhysicalMetadataTargetKind::STRING &&
                       (reference.referenceKind == PhysicalMetadataReferenceKind::RECORD_NAME ||
                        reference.referenceKind == PhysicalMetadataReferenceKind::METHOD_NAME ||
                        reference.referenceKind == PhysicalMetadataReferenceKind::FIELD_NAME);
            }));
        foundDuplicateHeavyFixture =
            foundDuplicateHeavyFixture || requiredReferenceCount > program.requiredStringInventory.size();
        EXPECT_EQ(program.requiredStringInventory.capacity(), program.requiredStringInventory.size());
    }
    EXPECT_TRUE(foundDuplicateHeavyFixture);
}

using Key = std::pair<PhysicalEntityKind, uint64_t>;

void AddAnnotationKeys(const std::vector<PhysicalAnnotation> &annotations, std::set<Key> &expected)
{
    for (const auto &annotation : annotations) {
        expected.emplace(PhysicalEntityKind::ANNOTATION, annotation.offset);
    }
}

TEST(PhysicalReaderTest, EveryPhysicalItemIsBackedByThePublishedTree)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue()) << physical.Error().Message();
    const auto structure = DynamicStructureReader::Build(document.Value()->Summary().path, document.Value()->File());
    ASSERT_TRUE(structure.HasValue()) << structure.Error().Message();

    std::set<Key> expected;
    for (const auto &record : physical.Value().records) {
        if (!record.isSynthetic) {
            expected.emplace(PhysicalEntityKind::RECORD, record.offset);
        }
        AddAnnotationKeys(record.annotations, expected);
        for (const auto &field : record.fields) {
            expected.emplace(PhysicalEntityKind::FIELD, field.offset);
            AddAnnotationKeys(field.annotations, expected);
        }
        for (const auto &method : record.methods) {
            expected.emplace(PhysicalEntityKind::METHOD, method.offset);
            AddAnnotationKeys(method.annotations, expected);
            if (method.codeRange.has_value()) {
                expected.emplace(PhysicalEntityKind::CODE, method.codeRange->offset);
            }
        }
    }
    for (const auto &literal : physical.Value().literalArrays) {
        expected.emplace(PhysicalEntityKind::LITERAL_ARRAY, literal.range.offset);
    }

    std::set<Key> observed;
    for (const auto *node : structure.Value()->NodesInIdOrder()) {
        for (const auto &backing : node->backing) {
            observed.emplace(backing.kind, backing.range.offset);
        }
    }
    for (const auto &key : expected) {
        EXPECT_NE(observed.find(key), observed.end())
            << "missing kind=" << static_cast<int>(key.first) << " offset=" << key.second;
    }
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
