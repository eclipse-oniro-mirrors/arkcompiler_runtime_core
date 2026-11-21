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

#include "app/command_line.h"
#include "app/document_loader.h"
#include "core/common/result.h"
#include "core/document/abc_document.h"
#include "core/index/structure_index.h"
#include "core/model/file_range.h"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

TEST(DocumentLoaderTest, ReportsStagesInExecutionOrder)
{
    std::vector<DocumentLoadStage> stages;
    DocumentLoadOptions options;
    options.onStage = [&stages](DocumentLoadStage stage) { stages.push_back(stage); };
    const auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, options);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const std::vector<DocumentLoadStage> expected {
        DocumentLoadStage::OPENING,   DocumentLoadStage::METHODS,   DocumentLoadStage::PHYSICAL,
        DocumentLoadStage::STRUCTURE, DocumentLoadStage::LOCATIONS, DocumentLoadStage::METADATA,
    };
    EXPECT_EQ(stages, expected);
}

TEST(DocumentLoaderTest, CancellationStopsBeforeOpeningAndBetweenStages)
{
    CancellationSource cancellation;
    cancellation.Cancel();
    DocumentLoadOptions options {cancellation.Token(), {}};
    const auto before = LoadExplorerDocument("/does-not-exist.abc", options);
    ASSERT_FALSE(before.HasValue());
    EXPECT_EQ(before.Error().Code(), ErrorCode::CANCELLED);

    CancellationSource during;
    std::vector<DocumentLoadStage> stages;
    const auto onStage = [&during, &stages](DocumentLoadStage stage) {
        stages.push_back(stage);
        if (stage == DocumentLoadStage::METHODS) {
            during.Cancel();
        }
    };
    options = {during.Token(), onStage};
    const auto result = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, options);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::CANCELLED);
    EXPECT_EQ(stages, (std::vector<DocumentLoadStage> {DocumentLoadStage::OPENING, DocumentLoadStage::METHODS}));
}

struct TestAbcHeader {
    std::array<uint8_t, 8U> magic;
    uint32_t checksum;
    std::array<uint8_t, 4U> version;
    uint32_t fileSize;
    std::array<uint32_t, 10U> remainingFields;
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

class TemporaryFile {
public:
    explicit TemporaryFile(const std::string &contents)
    {
        char path[] = "/tmp/ark_bytecode_explorer_XXXXXX";
        int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        auto written = write(fd, contents.data(), contents.size());
        EXPECT_EQ(written, static_cast<ssize_t>(contents.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryFile()
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

class TemporaryUnicodePathAbc {
public:
    explicit TemporaryUnicodePathAbc(const std::string &sourcePath)
    {
        std::error_code error;
        const auto temporaryRoot = std::filesystem::temp_directory_path(error);
        EXPECT_FALSE(error) << error.message();
        if (error) {
            return;
        }
        for (size_t attempt = 0U; attempt < 16U && !ownsDirectory_; ++attempt) {
            const auto token = std::to_string(getpid()) + "-" + std::to_string(attempt);
            const auto candidate = temporaryRoot / std::filesystem::u8path("ark_bytecode_explorer_" + token);
            error.clear();
            if (std::filesystem::create_directory(candidate, error)) {
                directory_ = candidate;
                ownsDirectory_ = true;
            }
        }
        EXPECT_TRUE(ownsDirectory_) << error.message();
        if (!ownsDirectory_) {
            return;
        }
        path_ = directory_ / std::filesystem::u8path("\u6837\u4f8b-\U0001f600.abc");
        std::filesystem::copy_file(std::filesystem::u8path(sourcePath), path_, std::filesystem::copy_options::none,
                                   error);
        EXPECT_FALSE(error) << error.message();
    }

    ~TemporaryUnicodePathAbc()
    {
        if (ownsDirectory_) {
            std::error_code error;
            std::filesystem::remove(path_, error);
            error.clear();
            std::filesystem::remove(directory_, error);
        }
    }

    std::string Utf8Path() const
    {
        return path_.u8string();
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
    bool ownsDirectory_ {false};
};

std::string ReadBytes(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST(BytecodeExplorerCoreTest, FileRangeUsesHalfOpenBounds)
{
    FileRange range {10, 4};

    EXPECT_TRUE(range.Contains(10));
    EXPECT_TRUE(range.Contains(13));
    EXPECT_FALSE(range.Contains(14));
    EXPECT_TRUE(range.IsValidFor(14));
}

TEST(BytecodeExplorerCoreTest, FileRangeRejectsOverflow)
{
    FileRange range {UINT64_MAX - 1, 4};

    EXPECT_FALSE(range.Contains(UINT64_MAX));
    EXPECT_FALSE(range.IsValidFor(UINT64_MAX));
}

TEST(StructureIndexTest, AddsAndFindsHierarchyByStableId)
{
    StructureIndex index;
    EntityNode file {1, 0, EntityKind::FILE, "sample.abc"};
    EntityNode module {2, 1, EntityKind::MODULE, "entry"};

    ASSERT_TRUE(index.Add(std::move(file)));
    ASSERT_TRUE(index.Add(std::move(module)));

    ASSERT_NE(index.Find(1), nullptr);
    ASSERT_NE(index.Find(2), nullptr);
    EXPECT_EQ(index.RootIds(), std::vector<EntityId>({1}));
    EXPECT_EQ(index.Find(1)->children, std::vector<EntityId>({2}));
    EXPECT_EQ(index.Count(EntityKind::MODULE), 1U);
}

TEST(StructureIndexTest, ConstVisitorTraversesEveryNodeWithoutImposingOrder)
{
    StructureIndex index;
    ASSERT_TRUE(index.Add({1U, INVALID_ENTITY_ID, EntityKind::FILE, "file"}));
    ASSERT_TRUE(index.Add({2U, 1U, EntityKind::MODULE, "module"}));
    ASSERT_TRUE(index.Add({3U, 2U, EntityKind::METHOD, "method"}));
    const auto &constIndex = index;
    std::set<EntityId> visited;

    constIndex.ForEachNode([&visited](const EntityNode &node) { visited.emplace(node.id); });

    EXPECT_EQ(visited, std::set<EntityId>({1U, 2U, 3U}));
}

TEST(StructureIndexTest, RejectsDuplicateIdAndMissingParent)
{
    StructureIndex index;

    ASSERT_TRUE(index.Add(EntityNode {1, 0, EntityKind::FILE, "sample.abc"}));
    EXPECT_FALSE(index.Add(EntityNode {1, 0, EntityKind::MODULE, "duplicate"}));
    EXPECT_FALSE(index.Add(EntityNode {2, 99, EntityKind::CLASS, "orphan"}));
    EXPECT_EQ(index.Size(), 1U);
}

TEST(StructureIndexTest, StoresSemanticAndPhysicalMetadataWithoutBackendHandles)
{
    StructureIndex index;
    EntityNode module {1, 0, EntityKind::MODULE, "entry"};
    module.internalName = "entry";
    module.origin = EntityOrigin::PHYSICAL;
    module.primaryRange = FileRange {16, 24};
    module.backing.push_back({PhysicalEntityKind::RECORD, FileRange {16, 24}, "entry"});
    module.diagnostics.push_back({DiagnosticSeverity::INFO, "reconstructed from module metadata", std::nullopt});

    ASSERT_TRUE(index.Add(std::move(module)));
    const auto *stored = index.Find(1);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->internalName, "entry");
    EXPECT_EQ(stored->origin, EntityOrigin::PHYSICAL);
    ASSERT_TRUE(stored->primaryRange.has_value());
    EXPECT_EQ(stored->backing.front().kind, PhysicalEntityKind::RECORD);
    EXPECT_EQ(stored->diagnostics.front().severity, DiagnosticSeverity::INFO);
}

TEST(BytecodeExplorerCoreTest, ResultStoresValueOrError)
{
    auto success = Result<int>::Success(42);
    auto failure = Result<int>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "invalid"));

    ASSERT_TRUE(success.HasValue());
    EXPECT_EQ(success.Value(), 42);
    ASSERT_FALSE(failure.HasValue());
    EXPECT_EQ(failure.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(BytecodeExplorerCoreTest, MissingAbcReturnsFileNotFound)
{
    auto result = AbcDocument::Open("/path/that/does/not/exist.abc");

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::FILE_NOT_FOUND);
}

TEST(BytecodeExplorerCoreTest, InvalidAbcReturnsInvalidFormat)
{
    TemporaryFile file("not-an-abc");

    auto result = AbcDocument::Open(file.Path());

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::INVALID_FORMAT);
}

TEST(BytecodeExplorerCoreTest, OpensDynamicArkTsV1Fixture)
{
    auto result = AbcDocument::Open(BYTECODE_EXPLORER_TEST_ABC);

    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    EXPECT_GT(result.Value()->Summary().fileSize, 0U);
    EXPECT_EQ(result.Value()->Summary().path, BYTECODE_EXPLORER_TEST_ABC);
}

TEST(BytecodeExplorerCoreTest, OpensUtf8PathWithBmpAndSupplementaryCharacters)
{
    TemporaryUnicodePathAbc fixture(BYTECODE_EXPLORER_TEST_ABC);
    const auto utf8Path = fixture.Utf8Path();

    auto result = AbcDocument::Open(utf8Path);

    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    EXPECT_EQ(result.Value()->Summary().path, utf8Path);
    EXPECT_GT(result.Value()->Summary().fileSize, 0U);

    auto loaded = LoadExplorerDocument(utf8Path);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    ASSERT_NE(loaded.Value().structure, nullptr);
    ASSERT_EQ(loaded.Value().structure->RootIds().size(), 1U);
    const auto *root = loaded.Value().structure->Find(loaded.Value().structure->RootIds().front());
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name, std::filesystem::u8path(utf8Path).filename().u8string());
}

TEST(BytecodeExplorerCoreTest, RejectsStaticAbcFixture)
{
    auto result = AbcDocument::Open(BYTECODE_EXPLORER_TEST_STATIC_ABC);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::UNSUPPORTED_FORMAT);
}

TEST(BytecodeExplorerCoreTest, RejectsUnsupportedDynamicVersionBeforeOpeningTheBackend)
{
    auto bytes = ReadBytes(BYTECODE_EXPLORER_TEST_ABC);
    ASSERT_GE(bytes.size(), sizeof(TestAbcHeader));
    TestAbcHeader header {};
    header = ReadPod<TestAbcHeader>(bytes.data());
    header.version = {UINT8_MAX, 0U, 0U, 0U};
    WritePod(bytes.data(), header);
    TemporaryFile file(bytes);

    const auto result = AbcDocument::Open(file.Path());
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::UNSUPPORTED_FORMAT);
}

TEST(CommandLineTest, ParsesVersionWithoutOpeningAbc)
{
    const char *argv[] = {"ark_bytecode_explorer", "--version"};

    auto result = ParseCommandLine(2, argv);

    ASSERT_TRUE(result.HasValue());
    EXPECT_TRUE(result.Value().showVersion);
    EXPECT_FALSE(result.Value().showHelp);
    EXPECT_FALSE(result.Value().abcPath.has_value());
}

TEST(CommandLineTest, ParsesSingleAbcPath)
{
    const char *argv[] = {"ark_bytecode_explorer", "sample.abc"};

    auto result = ParseCommandLine(2, argv);

    ASSERT_TRUE(result.HasValue());
    ASSERT_TRUE(result.Value().abcPath.has_value());
    EXPECT_EQ(*result.Value().abcPath, "sample.abc");
}

TEST(CommandLineTest, ParsesHeadlessStructureDumpOptions)
{
    const char *argv[] = {"ark_bytecode_explorer", "--dump-structure", "--show-internals", "sample.abc"};

    auto result = ParseCommandLine(4, argv);

    ASSERT_TRUE(result.HasValue());
    EXPECT_TRUE(result.Value().dumpStructure);
    EXPECT_TRUE(result.Value().showInternals);
    ASSERT_TRUE(result.Value().abcPath.has_value());
    EXPECT_EQ(*result.Value().abcPath, "sample.abc");
}

TEST(CommandLineTest, RejectsStructureDumpWithoutAbcPath)
{
    const char *argv[] = {"ark_bytecode_explorer", "--dump-structure"};

    auto result = ParseCommandLine(2, argv);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(CommandLineTest, RejectsUnknownOption)
{
    const char *argv[] = {"ark_bytecode_explorer", "--bad"};

    auto result = ParseCommandLine(2, argv);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(CommandLineTest, RejectsMultiplePaths)
{
    const char *argv[] = {"ark_bytecode_explorer", "first.abc", "second.abc"};

    auto result = ParseCommandLine(3, argv);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(CommandLineTest, ParsesBrowserWithoutAutomaticLaunch)
{
    const char *argv[] = {"ark_bytecode_explorer", "--no-open", "sample.abc"};
    auto result = ParseCommandLine(3, argv);
    ASSERT_TRUE(result.HasValue());
    EXPECT_TRUE(result.Value().noOpen);
    EXPECT_EQ(*result.Value().abcPath, "sample.abc");
}

TEST(CommandLineTest, AcceptsBrowserWithoutInputForTheEmptyPickerPage)
{
    const char *empty[] = {"ark_bytecode_explorer"};
    auto emptyResult = ParseCommandLine(1, empty);
    ASSERT_TRUE(emptyResult.HasValue());
    EXPECT_FALSE(emptyResult.Value().abcPath.has_value());
    EXPECT_FALSE(emptyResult.Value().noOpen);

    const char *noOpen[] = {"ark_bytecode_explorer", "--no-open"};
    auto noOpenResult = ParseCommandLine(2, noOpen);
    ASSERT_TRUE(noOpenResult.HasValue());
    EXPECT_FALSE(noOpenResult.Value().abcPath.has_value());
    EXPECT_TRUE(noOpenResult.Value().noOpen);
}

TEST(CommandLineTest, RejectsIncompatibleBrowserOptionsInEitherOrder)
{
    for (const auto *option : {"--help", "--version", "--dump-structure", "--no-open"}) {
        const char *first[] = {"ark_bytecode_explorer", "--no-open", option, "sample.abc"};
        const char *last[] = {"ark_bytecode_explorer", option, "--no-open", "sample.abc"};
        EXPECT_FALSE(ParseCommandLine(4, first).HasValue()) << option;
        EXPECT_FALSE(ParseCommandLine(4, last).HasValue()) << option;
    }
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
