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

#include "app/document_loader.h"
#include "core/metadata/metadata_catalog.h"
#include "core/reader/literal_array_reader.h"
#include "core/reader/physical_reader.h"
#include "core/reader/structure_classifier.h"

#include "libpandafile/method_data_accessor-inl.h"
#include "libpandafile/literal_data_accessor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <unistd.h>

namespace panda::bytecode_explorer::test {

struct MetadataBuildObservation {
    Result<std::shared_ptr<const MetadataCatalog>> result;
    size_t stringDecodeCount {0U};
    uint64_t peakConstructionBytes {0U};
    size_t referenceCountUpdates {0U};
    size_t requiredValidationReferenceVisits {0U};
    size_t diagnosticAdmissionChecks {0U};
};

class MetadataCatalogTestAccess {
public:
    static MetadataBuildObservation Build(const panda_file::File &file, const PhysicalProgram &physical,
                                          const StructureIndex &structure, MetadataLimits limits = {})
    {
        MetadataCatalog::BuildObserver observer;
        auto result = MetadataCatalog::BuildImpl(file, physical, structure, limits, &observer);
        return {std::move(result),
                observer.stringDecodeCount,
                observer.peakConstructionBytes,
                observer.referenceCountUpdates,
                observer.requiredValidationReferenceVisits,
                observer.diagnosticAdmissionChecks};
    }
};

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

class TemporaryTagValueLiteralAbc final {
public:
    explicit TemporaryTagValueLiteralAbc(const std::string &sourcePath)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        std::vector<char> bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_GE(bytes.size(), sizeof(TestAbcHeader));
        if (bytes.size() < sizeof(TestAbcHeader)) {
            return;
        }
        TestAbcHeader header {};
        header = ReadPod<TestAbcHeader>(bytes.data());
        EXPECT_GT(header.literalArrayCount, 0U);
        EXPECT_LE(static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t), bytes.size());
        if (header.literalArrayCount == 0U ||
            static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t) > bytes.size()) {
            return;
        }
        literalOffset_ = ReadPod<uint32_t>(bytes.data() + header.literalArrayIndexOffset);
        constexpr size_t patchSize = sizeof(uint32_t) + 3U + sizeof(uint32_t);
        EXPECT_LE(static_cast<uint64_t>(literalOffset_) + patchSize, bytes.size());
        if (static_cast<uint64_t>(literalOffset_) + patchSize > bytes.size()) {
            return;
        }
        stringOffset_ = static_cast<uint32_t>(bytes.size());
        bytes.push_back(static_cast<char>(2U));
        bytes.push_back('x');
        bytes.push_back('\0');
        header.fileSize = static_cast<uint32_t>(bytes.size());
        WritePod(bytes.data(), header);

        constexpr uint32_t valueCount = 4U;
        WritePod(bytes.data() + literalOffset_, valueCount);
        bytes[literalOffset_ + sizeof(uint32_t)] = static_cast<char>(panda_file::LiteralTag::TAGVALUE);
        bytes[literalOffset_ + sizeof(uint32_t) + 1U] = static_cast<char>(0x7F);
        bytes[literalOffset_ + sizeof(uint32_t) + 2U] = static_cast<char>(panda_file::LiteralTag::STRING);
        WritePod(bytes.data() + literalOffset_ + sizeof(uint32_t) + 3U, stringOffset_);

        char path[] = "/tmp/ark_bytecode_explorer_tag_value_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryTagValueLiteralAbc()
    {
        if (!path_.empty()) {
            EXPECT_EQ(unlink(path_.c_str()), 0);
        }
    }

    const std::string &Path() const
    {
        return path_;
    }

    uint32_t LiteralOffset() const
    {
        return literalOffset_;
    }

    uint32_t StringOffset() const
    {
        return stringOffset_;
    }

private:
    std::string path_;
    uint32_t literalOffset_ {0U};
    uint32_t stringOffset_ {0U};
};

class TemporaryTruncatedLiteralStringAbc final {
public:
    explicit TemporaryTruncatedLiteralStringAbc(const std::string &sourcePath)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        std::vector<char> bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_GE(bytes.size(), sizeof(TestAbcHeader));
        if (bytes.size() < sizeof(TestAbcHeader)) {
            return;
        }
        TestAbcHeader header {};
        header = ReadPod<TestAbcHeader>(bytes.data());
        EXPECT_GT(header.literalArrayCount, 0U);
        EXPECT_LE(static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t), bytes.size());
        if (header.literalArrayCount == 0U ||
            static_cast<uint64_t>(header.literalArrayIndexOffset) + sizeof(uint32_t) > bytes.size()) {
            return;
        }
        literalOffset_ = ReadPod<uint32_t>(bytes.data() + header.literalArrayIndexOffset);
        constexpr size_t patchSize = sizeof(uint32_t) + 1U + sizeof(uint32_t);
        EXPECT_LE(static_cast<uint64_t>(literalOffset_) + patchSize, bytes.size());
        if (static_cast<uint64_t>(literalOffset_) + patchSize > bytes.size()) {
            return;
        }
        stringOffset_ = static_cast<uint32_t>(bytes.size());
        bytes.push_back(static_cast<char>(0x80U));
        header.fileSize = static_cast<uint32_t>(bytes.size());
        WritePod(bytes.data(), header);

        constexpr uint32_t valueCount = 2U;
        WritePod(bytes.data() + literalOffset_, valueCount);
        bytes[literalOffset_ + sizeof(uint32_t)] = static_cast<char>(panda_file::LiteralTag::STRING);
        WritePod(bytes.data() + literalOffset_ + sizeof(uint32_t) + 1U, stringOffset_);

        char path[] = "/tmp/ark_bytecode_explorer_truncated_literal_string_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryTruncatedLiteralStringAbc()
    {
        if (!path_.empty()) {
            EXPECT_EQ(unlink(path_.c_str()), 0);
        }
    }

    const std::string &Path() const
    {
        return path_;
    }

    uint32_t LiteralOffset() const
    {
        return literalOffset_;
    }

    uint32_t StringOffset() const
    {
        return stringOffset_;
    }

private:
    std::string path_;
    uint32_t literalOffset_ {0U};
    uint32_t stringOffset_ {0U};
};

class TemporaryLongStringAbc final {
public:
    explicit TemporaryLongStringAbc(const std::string &sourcePath)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        std::vector<char> bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_GE(bytes.size(), sizeof(TestAbcHeader));
        if (bytes.size() < sizeof(TestAbcHeader)) {
            return;
        }
        TestAbcHeader header {};
        header = ReadPod<TestAbcHeader>(bytes.data());
        stringOffset_ = static_cast<uint32_t>(bytes.size());
        constexpr size_t textSize = 600U;
        constexpr uint32_t taggedUtf16Length = static_cast<uint32_t>(textSize << 1U);
        bytes.push_back(static_cast<char>((taggedUtf16Length & 0x7FU) | 0x80U));
        bytes.push_back(static_cast<char>(taggedUtf16Length >> 7U));
        bytes.insert(bytes.end(), textSize, 'A');
        bytes.push_back('\0');
        header.fileSize = static_cast<uint32_t>(bytes.size());
        WritePod(bytes.data(), header);

        char path[] = "/tmp/ark_bytecode_explorer_long_string_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryLongStringAbc()
    {
        if (!path_.empty()) {
            EXPECT_EQ(unlink(path_.c_str()), 0);
        }
    }

    const std::string &Path() const
    {
        return path_;
    }

    uint32_t StringOffset() const
    {
        return stringOffset_;
    }

private:
    std::string path_;
    uint32_t stringOffset_ {0U};
};

TEST(MetadataCatalogTest, BuildsThreeStableKindsFromTheRealDynamicAbc)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    ASSERT_NE(loaded.Value().metadata, nullptr);
    const auto &objects = loaded.Value().metadata->Objects();
    EXPECT_TRUE(
        std::is_sorted(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.key < b.key; }));
    EXPECT_NE(std::find_if(objects.begin(), objects.end(),
                           [](const auto &object) {
                               return object.key.kind == MetadataKind::STRING && object.preview == "create";
                           }),
              objects.end());
    EXPECT_NE(std::find_if(objects.begin(), objects.end(),
                           [](const auto &object) {
                               return object.key.kind == MetadataKind::METHOD && object.preview == "create";
                           }),
              objects.end());
    EXPECT_NE(std::find_if(objects.begin(), objects.end(),
                           [](const auto &object) { return object.key.kind == MetadataKind::LITERAL_ARRAY; }),
              objects.end());
}

TEST(MetadataCatalogTest, LiteralArraysWithUnknownExtentExposeNoNavigableRange)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    size_t literalArrays = 0U;
    for (const auto &object : loaded.Value().metadata->Objects()) {
        if (object.key.kind != MetadataKind::LITERAL_ARRAY) {
            continue;
        }
        ++literalArrays;
        EXPECT_FALSE(object.range.has_value());
    }
    EXPECT_GT(literalArrays, 0U);
}

TEST(MetadataCatalogTest, KeysAreUniqueRangesStayInsideTheFileAndTargetsExist)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    std::set<MetadataObjectKey> keys;
    for (const auto &object : loaded.Value().metadata->Objects()) {
        EXPECT_TRUE(keys.insert(object.key).second);
        if (object.range.has_value()) {
            EXPECT_GT(object.range->size, 0U);
            EXPECT_TRUE(object.range->IsValidFor(loaded.Value().summary.fileSize));
        }
    }
    for (const auto &reference : loaded.Value().metadata->StructuralReferences()) {
        EXPECT_NE(loaded.Value().metadata->Find(reference.target), nullptr);
        if (reference.sourceRange.has_value()) {
            EXPECT_GT(reference.sourceRange->size, 0U);
        }
    }
}

TEST(MetadataCatalogTest, BudgetExhaustionProducesAPartialCatalogWithoutFailingTheDocument)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    MetadataLimits limits;
    limits.maxPersistentBytes = 1U;
    auto catalog = MetadataCatalog::Build(document.Value()->File(), physical.Value(), *structure.Value(), limits);
    ASSERT_TRUE(catalog.HasValue());
    EXPECT_TRUE(catalog.Value()->IsPartial());
    EXPECT_FALSE(catalog.Value()->Diagnostics().empty());
}

TEST(MetadataCatalogTest, BudgetExhaustionDoesNotSuppressMandatoryValidation)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    MetadataLimits limits;
    limits.maxPersistentBytes = 1U;

    PhysicalProgram malformedString;
    malformedString.fileSize = physical.Value().fileSize;
    const auto required =
        std::find_if(physical.Value().metadataReferences.begin(), physical.Value().metadataReferences.end(),
                     [](const auto &reference) {
                         return reference.targetKind == PhysicalMetadataTargetKind::STRING &&
                                (reference.referenceKind == PhysicalMetadataReferenceKind::RECORD_NAME ||
                                 reference.referenceKind == PhysicalMetadataReferenceKind::METHOD_NAME ||
                                 reference.referenceKind == PhysicalMetadataReferenceKind::FIELD_NAME);
                     });
    ASSERT_NE(required, physical.Value().metadataReferences.end());
    auto malformedReference = *required;
    malformedReference.targetOffset = static_cast<uint32_t>(document.Value()->Summary().fileSize - 1U);
    malformedString.metadataReferences.push_back(malformedReference);
    malformedString.requiredStringInventory.push_back({malformedReference.targetOffset, 0U});
    auto invalidStringCatalog =
        MetadataCatalog::Build(document.Value()->File(), malformedString, *structure.Value(), limits);
    EXPECT_FALSE(invalidStringCatalog.HasValue());

    StructureIndex invalidStructure;
    EntityNode invalidNode {1U, INVALID_ENTITY_ID, EntityKind::FILE, "file"};
    invalidNode.backing.push_back(
        {PhysicalEntityKind::FILE, FileRange {document.Value()->Summary().fileSize + 1U, 0U}, "outside"});
    ASSERT_TRUE(invalidStructure.Add(std::move(invalidNode)));
    auto invalidStructureCatalog =
        MetadataCatalog::Build(document.Value()->File(), physical.Value(), invalidStructure, limits);
    EXPECT_FALSE(invalidStructureCatalog.HasValue());
}

TEST(MetadataCatalogTest, InventoryOnlyLiteralArraysRetainValidatedSlotEvidenceWithoutInventingExtent)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    std::set<uint32_t> inventory;
    std::set<uint32_t> excludedOffsets;
    for (const auto &record : physical.Value().records) {
        if (record.moduleDataRange.has_value()) {
            excludedOffsets.emplace(static_cast<uint32_t>(record.moduleDataRange->offset));
        }
    }
    for (const auto &literal : physical.Value().literalArrays) {
        inventory.emplace(static_cast<uint32_t>(literal.range.offset));
    }

    auto inventoryOnly = LiteralArrayReader::Read(document.Value()->File(), inventory, {}, excludedOffsets);
    ASSERT_EQ(inventoryOnly.size(), physical.Value().literalArrays.size());
    for (const auto &source : physical.Value().literalArrays) {
        const auto snapshot = std::find_if(
            inventoryOnly.begin(), inventoryOnly.end(),
            [sourceOffset = source.range.offset](const auto &literal) { return literal.range.offset == sourceOffset; });
        ASSERT_NE(snapshot, inventoryOnly.end());
        EXPECT_EQ(snapshot->range.size, 0U);
        if (excludedOffsets.find(static_cast<uint32_t>(source.range.offset)) != excludedOffsets.end()) {
            EXPECT_TRUE(snapshot->metadataReferences.empty());
            continue;
        }
        ASSERT_EQ(snapshot->metadataReferences.size(), source.metadataReferences.size());
        for (size_t index = 0U; index < source.metadataReferences.size(); ++index) {
            const auto &expected = source.metadataReferences[index];
            const auto &observed = snapshot->metadataReferences[index];
            EXPECT_EQ(observed.targetKind, expected.targetKind);
            EXPECT_EQ(observed.referenceKind, expected.referenceKind);
            EXPECT_EQ(observed.targetOffset, expected.targetOffset);
            EXPECT_EQ(observed.ordinal, expected.ordinal);
        }
    }
}

TEST(MetadataCatalogTest, InventoryOnlyLiteralArrayConsumesTagValueAsOneByteWithoutMetadataEvidence)
{
    TemporaryTagValueLiteralAbc fixture(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    auto document = AbcDocument::Open(fixture.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    std::vector<PhysicalLiteralArray> literals;

    EXPECT_NO_THROW(literals = LiteralArrayReader::Read(document.Value()->File(), {fixture.LiteralOffset()}, {}, {}));

    ASSERT_EQ(literals.size(), 1U);
    EXPECT_EQ(literals.front().valueCount, 4U);
    ASSERT_EQ(literals.front().metadataReferences.size(), 1U);
    EXPECT_EQ(literals.front().metadataReferences.front().targetOffset, fixture.StringOffset());
    EXPECT_EQ(literals.front().metadataReferences.front().ordinal, 2U);
    EXPECT_TRUE(literals.front().methods.empty());
    EXPECT_TRUE(literals.front().nestedArrayOffsets.empty());
}

TEST(MetadataCatalogTest, LiteralStringEvidenceDoesNotDecodeItsOptionalTarget)
{
    TemporaryTruncatedLiteralStringAbc fixture(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    auto document = AbcDocument::Open(fixture.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    std::vector<PhysicalLiteralArray> literals;

    EXPECT_NO_THROW(literals = LiteralArrayReader::Read(document.Value()->File(), {fixture.LiteralOffset()}, {}, {}));

    ASSERT_EQ(literals.size(), 1U);
    ASSERT_EQ(literals.front().metadataReferences.size(), 1U);
    EXPECT_EQ(literals.front().metadataReferences.front().targetOffset, fixture.StringOffset());
    EXPECT_EQ(literals.front().metadataReferences.front().referenceKind, PhysicalMetadataReferenceKind::LITERAL_STRING);

    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue()) << physical.Error().Message();
    const auto literal = std::find_if(physical.Value().literalArrays.begin(), physical.Value().literalArrays.end(),
                                      [literalOffset = fixture.LiteralOffset()](const auto &candidate) {
                                          return candidate.range.offset == literalOffset;
                                      });
    ASSERT_NE(literal, physical.Value().literalArrays.end());
    ASSERT_EQ(literal->metadataReferences.size(), 1U);
    EXPECT_EQ(literal->metadataReferences.front().targetOffset, fixture.StringOffset());
}

TEST(MetadataCatalogTest, LiteralEvidenceUsesBoundedSlotContextInsteadOfTargetStringContent)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    size_t referenceCount = 0U;

    for (const auto &literal : physical.Value().literalArrays) {
        for (const auto &reference : literal.metadataReferences) {
            const auto expected =
                "literal array " + std::to_string(literal.range.offset) + " slot " + std::to_string(reference.ordinal);
            EXPECT_EQ(reference.sourceName, expected);
            EXPECT_LE(reference.sourceName.size(), 64U);
            ++referenceCount;
        }
    }
    EXPECT_GT(referenceCount, 0U);
}

TEST(MetadataCatalogTest, StructureAssociationUsesPhysicalKindAndOffset)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    ASSERT_FALSE(physical.Value().literalArrays.empty());
    const auto literalOffset = physical.Value().literalArrays.front().range.offset;

    StructureIndex structure;
    ASSERT_TRUE(structure.Add({1U, INVALID_ENTITY_ID, EntityKind::FILE, "file"}));
    EntityNode wrongKind {2U, 1U, EntityKind::ABC_ITEM, "wrong kind"};
    wrongKind.backing.push_back({PhysicalEntityKind::METHOD, FileRange {literalOffset, 0U}, "method collision"});
    ASSERT_TRUE(structure.Add(std::move(wrongKind)));
    EntityNode exactKind {3U, 1U, EntityKind::ABC_ITEM, "literal"};
    exactKind.backing.push_back({PhysicalEntityKind::LITERAL_ARRAY, FileRange {literalOffset, 0U}, "literal"});
    ASSERT_TRUE(structure.Add(std::move(exactKind)));

    auto catalog = MetadataCatalog::Build(document.Value()->File(), physical.Value(), structure);
    ASSERT_TRUE(catalog.HasValue()) << catalog.Error().Message();
    const auto *literal = catalog.Value()->Find({MetadataKind::LITERAL_ARRAY, literalOffset});
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->entityIds, std::vector<EntityId>({3U}));
}

TEST(MetadataCatalogTest, LiteralOnlyMethodFallbackUsesTheBoundedStringReader)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_NO_SCOPE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    const auto source = std::find_if(physical.Value().literalArrays.begin(), physical.Value().literalArrays.end(),
                                     [](const auto &literal) { return !literal.methods.empty(); });
    ASSERT_NE(source, physical.Value().literalArrays.end());
    EXPECT_TRUE(std::all_of(source->methods.begin(), source->methods.end(),
                            [](const auto &method) { return method.name.empty(); }));

    auto literal = *source;
    literal.methods = {literal.methods.front()};
    literal.metadataReferences.clear();
    const auto methodOffset = literal.methods.front().offset;
    panda_file::MethodDataAccessor method(document.Value()->File(), panda_file::File::EntityId(methodOffset));
    const auto nameOffset = method.GetNameId().GetOffset();
    auto expectedName = ReadMetadataString(document.Value()->File(), nameOffset, MetadataLimits::MAX_PREVIEW_BYTES);
    ASSERT_TRUE(expectedName.HasValue());

    physical.Value().records.clear();
    physical.Value().literalArrays = {std::move(literal)};
    physical.Value().metadataReferences.clear();
    physical.Value().requiredStringInventory.clear();
    StructureIndex structure;
    ASSERT_TRUE(structure.Add({1U, INVALID_ENTITY_ID, EntityKind::FILE, "file"}));

    auto catalog = MetadataCatalog::Build(document.Value()->File(), physical.Value(), structure);
    ASSERT_TRUE(catalog.HasValue()) << catalog.Error().Message();
    const auto *methodObject = catalog.Value()->Find({MetadataKind::METHOD, methodOffset});
    ASSERT_NE(methodObject, nullptr);
    EXPECT_EQ(methodObject->preview, expectedName.Value().text);
    const auto *nameObject = catalog.Value()->Find({MetadataKind::STRING, nameOffset});
    ASSERT_NE(nameObject, nullptr);
    EXPECT_EQ(nameObject->preview, expectedName.Value().text);
}

TEST(MetadataCatalogTest, RepeatedStringReferencesDecodeEachOffsetOnlyOnce)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    auto baseline = MetadataCatalogTestAccess::Build(document.Value()->File(), physical.Value(), *structure.Value());
    ASSERT_TRUE(baseline.result.HasValue());
    const auto cleanPhysical = physical.Value();

    const auto source =
        std::find_if(physical.Value().metadataReferences.begin(), physical.Value().metadataReferences.end(),
                     [](const auto &reference) { return reference.targetKind == PhysicalMetadataTargetKind::STRING; });
    ASSERT_NE(source, physical.Value().metadataReferences.end());
    const auto sourceReference = *source;
    constexpr size_t duplicateCount = 4096U;
    physical.Value().metadataReferences.insert(physical.Value().metadataReferences.end(), duplicateCount,
                                               sourceReference);

    auto duplicate = MetadataCatalogTestAccess::Build(document.Value()->File(), physical.Value(), *structure.Value());
    ASSERT_TRUE(duplicate.result.HasValue());
    EXPECT_EQ(duplicate.stringDecodeCount, baseline.stringDecodeCount);

    auto invalidReference = sourceReference;
    invalidReference.referenceKind = PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE;
    invalidReference.targetOffset = static_cast<uint32_t>(document.Value()->Summary().fileSize - 1U);
    auto oneInvalid = cleanPhysical;
    oneInvalid.metadataReferences.push_back(invalidReference);
    auto oneInvalidBuild = MetadataCatalogTestAccess::Build(document.Value()->File(), oneInvalid, *structure.Value());
    ASSERT_TRUE(oneInvalidBuild.result.HasValue());
    auto repeatedInvalid = cleanPhysical;
    repeatedInvalid.metadataReferences.insert(repeatedInvalid.metadataReferences.end(), duplicateCount,
                                              invalidReference);
    auto repeatedInvalidBuild =
        MetadataCatalogTestAccess::Build(document.Value()->File(), repeatedInvalid, *structure.Value());
    ASSERT_TRUE(repeatedInvalidBuild.result.HasValue());
    EXPECT_EQ(repeatedInvalidBuild.stringDecodeCount, oneInvalidBuild.stringDecodeCount);
}

TEST(MetadataCatalogTest, RequiredStringValidationUsesCompactLinearInventoryUnderExhaustion)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto sourcePhysical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(sourcePhysical.HasValue());
    const auto required = std::find_if(sourcePhysical.Value().metadataReferences.begin(),
                                       sourcePhysical.Value().metadataReferences.end(), [](const auto &reference) {
                                           return reference.targetKind == PhysicalMetadataTargetKind::STRING &&
                                                  reference.referenceKind == PhysicalMetadataReferenceKind::METHOD_NAME;
                                       });
    ASSERT_NE(required, sourcePhysical.Value().metadataReferences.end());
    constexpr size_t referenceCount = 16384U;
    PhysicalProgram physical;
    physical.fileSize = sourcePhysical.Value().fileSize;
    physical.metadataReferences.assign(referenceCount, *required);
    physical.requiredStringInventory = {{required->targetOffset, 0U}};
    StructureIndex structure;
    MetadataLimits limits;
    limits.maxPersistentBytes = 1U;

    auto observed = MetadataCatalogTestAccess::Build(document.Value()->File(), physical, structure, limits);

    ASSERT_TRUE(observed.result.HasValue()) << observed.result.Error().Message();
    EXPECT_TRUE(observed.result.Value()->IsPartial());
    EXPECT_EQ(observed.stringDecodeCount, 1U);
    EXPECT_LE(observed.requiredValidationReferenceVisits, referenceCount + 1U);
    EXPECT_LE(observed.peakConstructionBytes, limits.maxPersistentBytes);
}

bool IsOptionalStringReference(const PhysicalMetadataReference &ref, const PhysicalProgram &program)
{
    const bool required = ref.referenceKind == PhysicalMetadataReferenceKind::RECORD_NAME ||
                          ref.referenceKind == PhysicalMetadataReferenceKind::METHOD_NAME ||
                          ref.referenceKind == PhysicalMetadataReferenceKind::FIELD_NAME;
    const bool alreadyInventoried =
        std::any_of(program.requiredStringInventory.begin(), program.requiredStringInventory.end(),
                    [targetOffset = ref.targetOffset](const auto &entry) { return entry.offset == targetOffset; });
    return ref.targetKind == PhysicalMetadataTargetKind::STRING && !required && !alreadyInventoried;
}

const PhysicalMetadataReference *FindOptionalStringReference(const PhysicalProgram &program)
{
    for (const auto &ref : program.metadataReferences) {
        if (IsOptionalStringReference(ref, program)) {
            return &ref;
        }
    }
    return nullptr;
}

TEST(MetadataCatalogTest, RequiredStringInventoryRejectsAStaleExtraEntry)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    const auto *optional = FindOptionalStringReference(physical.Value());
    ASSERT_NE(optional, nullptr);
    const auto representative = static_cast<size_t>(optional - physical.Value().metadataReferences.data());
    auto stale = physical.Value();
    stale.requiredStringInventory.push_back({optional->targetOffset, representative});
    std::sort(stale.requiredStringInventory.begin(), stale.requiredStringInventory.end(),
              [](const auto &left, const auto &right) { return left.offset < right.offset; });

    auto catalog = MetadataCatalog::Build(document.Value()->File(), stale, *structure.Value());

    EXPECT_FALSE(catalog.HasValue());
}

TEST(MetadataCatalogTest, RequiredStringInventoryRejectsMissingOutOfRangeAndRemovedRepresentatives)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    ASSERT_FALSE(physical.Value().requiredStringInventory.empty());

    auto missing = physical.Value();
    missing.requiredStringInventory.erase(missing.requiredStringInventory.begin());
    auto missingCatalog = MetadataCatalog::Build(document.Value()->File(), missing, *structure.Value());
    EXPECT_FALSE(missingCatalog.HasValue());

    auto outOfRange = physical.Value();
    outOfRange.requiredStringInventory.front().representativeReferenceIndex = std::numeric_limits<size_t>::max();
    auto outOfRangeCatalog = MetadataCatalog::Build(document.Value()->File(), outOfRange, *structure.Value());
    EXPECT_FALSE(outOfRangeCatalog.HasValue());

    auto removed = physical.Value();
    const auto representative = removed.requiredStringInventory.front().representativeReferenceIndex;
    removed.metadataReferences.resize(representative);
    auto removedCatalog = MetadataCatalog::Build(document.Value()->File(), removed, *structure.Value());
    EXPECT_FALSE(removedCatalog.HasValue());
}

TEST(MetadataCatalogTest, RequiredStringInventoryRejectsAMismatchedRepresentative)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    ASSERT_FALSE(physical.Value().requiredStringInventory.empty());
    auto kindMismatch = physical.Value();
    const auto representative = kindMismatch.requiredStringInventory.front().representativeReferenceIndex;
    ASSERT_LT(representative, kindMismatch.metadataReferences.size());
    kindMismatch.metadataReferences[representative].referenceKind =
        PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE;
    auto kindMismatchCatalog = MetadataCatalog::Build(document.Value()->File(), kindMismatch, *structure.Value());
    EXPECT_FALSE(kindMismatchCatalog.HasValue());

    auto offsetMismatch = physical.Value();
    offsetMismatch.metadataReferences[representative].targetOffset =
        offsetMismatch.requiredStringInventory.front().offset == 0U
            ? 1U
            : offsetMismatch.requiredStringInventory.front().offset - 1U;
    auto offsetMismatchCatalog = MetadataCatalog::Build(document.Value()->File(), offsetMismatch, *structure.Value());

    EXPECT_FALSE(offsetMismatchCatalog.HasValue());
}

TEST(MetadataCatalogTest, DiagnosticAdmissionStopsPermanentlyAfterBudgetExhaustion)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    PhysicalProgram physical;
    physical.fileSize = document.Value()->Summary().fileSize;
    constexpr size_t referenceCount = 16384U;
    physical.metadataReferences.reserve(referenceCount);
    for (size_t index = 0U; index < referenceCount; ++index) {
        PhysicalMetadataReference reference;
        reference.targetKind = PhysicalMetadataTargetKind::STRING;
        reference.referenceKind = PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE;
        reference.targetOffset = static_cast<uint32_t>(physical.fileSize + index + 1U);
        physical.metadataReferences.push_back(std::move(reference));
    }
    StructureIndex structure;
    MetadataLimits limits;
    limits.maxPersistentBytes = 512U;

    auto observed = MetadataCatalogTestAccess::Build(document.Value()->File(), physical, structure, limits);

    ASSERT_TRUE(observed.result.HasValue()) << observed.result.Error().Message();
    EXPECT_TRUE(observed.result.Value()->IsPartial());
    EXPECT_LE(observed.diagnosticAdmissionChecks, 8U);
    EXPECT_LE(observed.peakConstructionBytes, limits.maxPersistentBytes);
}

TEST(MetadataCatalogTest, BoundedStringReaderReservesOnlyItsRetainedPreviewCapacity)
{
    TemporaryLongStringAbc fixture(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    auto document = AbcDocument::Open(fixture.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();

    auto decoded =
        ReadMetadataString(document.Value()->File(), fixture.StringOffset(), MetadataLimits::MAX_PREVIEW_BYTES);

    ASSERT_TRUE(decoded.HasValue()) << decoded.Error().Message();
    EXPECT_TRUE(decoded.Value().truncated);
    EXPECT_EQ(decoded.Value().text.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    // reserve() may round up to an implementation-defined capacity. Compare against an equally sized retained
    // string so this still proves the reader did not reserve for the full encoded input.
    const std::string retainedPreview(MetadataLimits::MAX_PREVIEW_BYTES, 'A');
    EXPECT_LE(decoded.Value().text.capacity(), retainedPreview.capacity());

    PhysicalProgram physical;
    physical.fileSize = document.Value()->Summary().fileSize;
    PhysicalMetadataReference reference;
    reference.targetKind = PhysicalMetadataTargetKind::STRING;
    reference.referenceKind = PhysicalMetadataReferenceKind::RECORD_NAME;
    reference.targetOffset = fixture.StringOffset();
    physical.metadataReferences.push_back(reference);
    physical.requiredStringInventory.push_back({fixture.StringOffset(), 0U});
    StructureIndex structure;
    MetadataLimits limits;
    limits.maxPersistentBytes = 4096U;

    auto observed = MetadataCatalogTestAccess::Build(document.Value()->File(), physical, structure, limits);

    ASSERT_TRUE(observed.result.HasValue()) << observed.result.Error().Message();
    const auto *object = observed.result.Value()->Find({MetadataKind::STRING, fixture.StringOffset()});
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->preview.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_LE(observed.peakConstructionBytes, limits.maxPersistentBytes);
}

TEST(MetadataCatalogTest, BoundedReferenceNamesEndOnAUtf8Boundary)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue()) << physical.Error().Message();
    ASSERT_FALSE(physical.Value().metadataReferences.empty());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue()) << structure.Error().Message();

    const std::string expected(MetadataLimits::MAX_PREVIEW_BYTES - 1U, 'A');
    physical.Value().metadataReferences.front().sourceName = expected + "\xE4\xB8\xAD";
    auto catalog = MetadataCatalog::Build(document.Value()->File(), physical.Value(), *structure.Value());

    ASSERT_TRUE(catalog.HasValue()) << catalog.Error().Message();
    const auto found =
        std::find_if(catalog.Value()->StructuralReferences().begin(), catalog.Value()->StructuralReferences().end(),
                     [expected](const auto &reference) { return reference.sourceName.rfind(expected, 0U) == 0U; });
    ASSERT_NE(found, catalog.Value()->StructuralReferences().end());
    EXPECT_EQ(found->sourceName, expected);
}

TEST(MetadataCatalogTest, ConstructionWorkspaceStaysInsideTheConfiguredBudget)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    ASSERT_FALSE(physical.Value().metadataReferences.empty());
    constexpr size_t duplicateCount = 32768U;
    physical.Value().metadataReferences.insert(physical.Value().metadataReferences.end(), duplicateCount,
                                               physical.Value().metadataReferences.front());
    MetadataLimits limits;
    limits.maxPersistentBytes = 64U * 1024U;

    auto observed =
        MetadataCatalogTestAccess::Build(document.Value()->File(), physical.Value(), *structure.Value(), limits);
    ASSERT_TRUE(observed.result.HasValue());
    EXPECT_TRUE(observed.result.Value()->IsPartial());
    EXPECT_LE(observed.result.Value()->EstimatedBytes(), limits.maxPersistentBytes);
    EXPECT_LE(observed.peakConstructionBytes, limits.maxPersistentBytes);
}

TEST(MetadataCatalogTest, PartialCatalogCountsEachAdmittedReferenceOnce)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    ASSERT_FALSE(physical.Value().metadataReferences.empty());
    constexpr size_t duplicateCount = 16384U;
    physical.Value().metadataReferences.insert(physical.Value().metadataReferences.end(), duplicateCount,
                                               physical.Value().metadataReferences.front());
    MetadataLimits limits;
    limits.maxPersistentBytes = 256U * 1024U;

    auto observed =
        MetadataCatalogTestAccess::Build(document.Value()->File(), physical.Value(), *structure.Value(), limits);
    ASSERT_TRUE(observed.result.HasValue());
    ASSERT_TRUE(observed.result.Value()->IsPartial());
    ASSERT_FALSE(observed.result.Value()->StructuralReferences().empty());
    for (const auto &reference : observed.result.Value()->StructuralReferences()) {
        EXPECT_NE(observed.result.Value()->Find(reference.target), nullptr);
    }
    for (const auto &object : observed.result.Value()->Objects()) {
        const auto admitted = static_cast<size_t>(
            std::count_if(observed.result.Value()->StructuralReferences().begin(),
                          observed.result.Value()->StructuralReferences().end(),
                          [key = object.key](const auto &reference) { return reference.target == key; }));
        EXPECT_EQ(object.referenceCount, admitted);
    }
    EXPECT_EQ(observed.referenceCountUpdates, observed.result.Value()->StructuralReferences().size());
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
