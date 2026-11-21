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
#include "core/metadata/metadata_index_service.h"
#include "core/reader/physical_reader.h"
#include "core/reader/structure_classifier.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer::test {

using MetadataIndexTestingFactory = std::shared_ptr<MetadataIndexService> (*)(std::shared_ptr<const MetadataCatalog>,
                                                                              std::vector<MethodMetadata>,
                                                                              MetadataReferenceIndexer::DecodeMethod,
                                                                              MetadataIndexLimits);
[[maybe_unused]] constexpr MetadataIndexTestingFactory METADATA_INDEX_TESTING_FACTORY =
    &MetadataIndexService::CreateForTesting;

class MetadataIndexServiceTestAccess {
private:
    class ThrowOnMovePost {
    public:
        ThrowOnMovePost() = default;
        ThrowOnMovePost(const ThrowOnMovePost &)
        {
            throw std::runtime_error("post callable copied outside boundary");
        }
        ThrowOnMovePost &operator=(const ThrowOnMovePost &) = delete;
        ThrowOnMovePost(ThrowOnMovePost &&)
        {
            throw std::runtime_error("post callable moved outside boundary");
        }
        ThrowOnMovePost &operator=(ThrowOnMovePost &&) = delete;

        void operator()() const
        {
            throw std::runtime_error("post callable invoked inside boundary");
        }
    };

    template <class T>
    static auto HasStoredBeforePost(int) -> decltype((void)&T::beforePostForTesting_, std::true_type {});

    template <class>
    static auto HasStoredBeforePost(...) -> std::false_type;

    template <class T>
    static auto HasStoredBeforePublish(int) -> decltype((void)&T::beforePublishForTesting_, std::true_type {});

    template <class>
    static auto HasStoredBeforePublish(...) -> std::false_type;

public:
    using DiagnosticsSnapshot = decltype(std::declval<MetadataIndexService>().diagnostics_);
    static_assert(std::is_same_v<DiagnosticsSnapshot, std::shared_ptr<const std::vector<std::string>>>);
    static_assert(std::is_same_v<typename MetadataIndexService::ReferenceSourceKeyLess::is_transparent, void>);
    static_assert(!decltype(HasStoredBeforePost<MetadataIndexService>(0))::value);
    static_assert(!decltype(HasStoredBeforePublish<MetadataIndexService>(0))::value);

    static void SetDiagnostics(const std::shared_ptr<MetadataIndexService> &service,
                               std::vector<std::string> diagnostics)
    {
        service->diagnostics_ = std::make_shared<const std::vector<std::string>>(std::move(diagnostics));
    }

    static bool StartWithBeforePostFailure(const std::shared_ptr<MetadataIndexService> &service,
                                           std::function<void()> failure)
    {
        return service->StartWithPost(std::move(failure));
    }

    static bool StartWithWorkerPublishFailure(const std::shared_ptr<MetadataIndexService> &service,
                                              std::function<void()> failure)
    {
        return service->StartWithPublishHookForTesting(std::move(failure));
    }

    static bool StartWithThrowOnMovePost(const std::shared_ptr<MetadataIndexService> &service)
    {
        return service->StartWithPost(ThrowOnMovePost {});
    }
};

namespace {

using namespace std::chrono_literals;

LoadedDocument LoadFixture()
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    EXPECT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    return loaded.HasValue() ? std::move(loaded.Value()) : LoadedDocument {};
}

DisassemblyResult MakeDisassembly(uint64_t methodOffset, std::vector<IdOperand> ids,
                                  std::string mnemonic = "load.metadata")
{
    auto disassembly = std::make_shared<FunctionDisassembly>();
    disassembly->methodOffset = methodOffset;
    InstructionInfo instruction;
    instruction.pc = 4U;
    instruction.fileRange = {0x700U + methodOffset, 2U};
    instruction.mnemonic = std::move(mnemonic);
    for (auto &id : ids) {
        instruction.ids.push_back(std::move(id));
    }
    disassembly->instructions.push_back(std::move(instruction));
    return {DisassemblyStatus::SUCCESS, std::move(disassembly), {}};
}

template <class Predicate>
bool WaitUntil(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

bool HasReliableRange(const MetadataReference &reference)
{
    return reference.sourceRange.has_value() && reference.sourceRange->size != 0U;
}

bool SameSemanticSource(const MetadataReference &left, const MetadataReference &right)
{
    if (!(left.target == right.target) || left.kind != right.kind ||
        left.sourceMethodOffset != right.sourceMethodOffset || left.operandIndex != right.operandIndex ||
        HasReliableRange(left) != HasReliableRange(right)) {
        return false;
    }
    if (HasReliableRange(left)) {
        return left.sourceRange->offset == right.sourceRange->offset &&
               left.sourceRange->size == right.sourceRange->size;
    }
    return left.pc == right.pc;
}

std::vector<std::string> ExceptionDiagnosticMessages()
{
    return {std::string(1024U * 1024U, 'x'),
            std::string("safe") + std::string(1U, static_cast<char>(0x80U)) + "ignored"};
}

void ExpectBoundedExceptionDiagnostic(const std::string &diagnostic, std::string_view prefix, bool huge)
{
    EXPECT_LE(diagnostic.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_EQ(diagnostic.substr(0U, prefix.size()), prefix);
    if (huge) {
        EXPECT_EQ(diagnostic.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    } else {
        EXPECT_EQ(diagnostic, std::string(prefix) + "safe");
    }
}

class DecodeGate final {
public:
    DisassemblyResult Decode(uint64_t methodOffset, const CancellationToken &cancellation)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const size_t call = ++entered_;
        condition_.notify_all();
        while (released_ < call && !cancellation.IsCancellationRequested()) {
            condition_.wait_for(lock, 1ms);
        }
        if (cancellation.IsCancellationRequested()) {
            exitedOnCancellation_ = true;
            condition_.notify_all();
            return {DisassemblyStatus::CANCELLED, nullptr, "cancelled"};
        }
        lock.unlock();
        return MakeDisassembly(methodOffset,
                               {{IdOperandKind::STRING, IdResolutionMode::INDEX, static_cast<uint32_t>(call),
                                 0x900U + call, "progress" + std::to_string(call)}});
    }

    bool WaitForEntered(size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, 2s, [this, count] { return entered_ >= count; });
    }

    void Release(size_t count)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = count;
        }
        condition_.notify_all();
    }

    bool WaitForCancellationExit()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] { return exitedOnCancellation_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    size_t entered_ {0U};
    size_t released_ {0U};
    bool exitedOnCancellation_ {false};
};

TEST(MetadataIndexServiceTest, StartsOnceAndPublishesMonotonicProgressUntilReady)
{
    auto loaded = LoadFixture();
    auto gate = std::make_shared<DecodeGate>();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x200U, 2U}},
        [gate](uint64_t offset, const CancellationToken &token) { return gate->Decode(offset, token); }, limits);
    ASSERT_NE(service, nullptr);

    const auto initial = service->Status();
    EXPECT_EQ(initial.phase, MetadataIndexPhase::NOT_STARTED);
    EXPECT_EQ(initial.revision, 0U);
    EXPECT_EQ(initial.scannedMethods, 0U);
    EXPECT_EQ(initial.totalMethods, 2U);
    ASSERT_TRUE(service->Start());
    EXPECT_FALSE(service->Start());
    ASSERT_TRUE(gate->WaitForEntered(1U));
    const auto building = service->Status();
    EXPECT_EQ(building.phase, MetadataIndexPhase::BUILDING);
    EXPECT_GE(building.scannedMethods, initial.scannedMethods);

    gate->Release(1U);
    ASSERT_TRUE(WaitUntil([service] { return service->Status().revision >= 1U; }));
    const auto first = service->Status();
    EXPECT_EQ(first.phase, MetadataIndexPhase::BUILDING);
    EXPECT_GE(first.scannedMethods, building.scannedMethods);
    EXPECT_GE(first.objectCount, building.objectCount);
    EXPECT_GE(first.referenceCount, building.referenceCount);
    ASSERT_TRUE(gate->WaitForEntered(2U));

    gate->Release(2U);
    service->WaitForIdleForTesting();
    const auto ready = service->Status();
    EXPECT_EQ(ready.phase, MetadataIndexPhase::READY);
    EXPECT_EQ(ready.scannedMethods, 2U);
    EXPECT_GE(ready.revision, 2U);
    EXPECT_GE(ready.objectCount, first.objectCount);
    EXPECT_GE(ready.referenceCount, first.referenceCount);
}

TEST(MetadataIndexServiceTest, StatusReportsDiagnosticsTruncatedFromTheCompleteSnapshot)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);
    std::vector<std::string> diagnostics;
    for (size_t index = 0U; index < 65U; ++index) {
        diagnostics.push_back("snapshot-diagnostic-" + std::to_string(index));
    }
    MetadataIndexServiceTestAccess::SetDiagnostics(service, std::move(diagnostics));

    const auto status = service->Status();

    EXPECT_EQ(status.diagnostics.size(), 64U);
    EXPECT_TRUE(status.diagnosticsTruncated);
}

TEST(MetadataIndexServiceTest, SmallSharedBudgetProducesPartialWithDiagnostic)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes();
    limits.batchMethods = 1U;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}},
        [](uint64_t offset, const CancellationToken &) {
            return MakeDisassembly(offset,
                                   {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xA00U, "over budget"}});
        },
        limits);

    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();
    const auto status = service->Status();

    EXPECT_EQ(status.phase, MetadataIndexPhase::PARTIAL);
    EXPECT_FALSE(status.diagnostics.empty());
    EXPECT_LE(service->EstimatedPersistentBytesForTesting(), limits.maxPersistentBytes);
}

TEST(MetadataIndexServiceTest, RetainedTerminalDiagnosticIsChargedToTheSharedBudget)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() + 4096U;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}},
        [](uint64_t, const CancellationToken &) -> DisassemblyResult { throw std::runtime_error("decode exploded"); },
        limits);

    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();
    const auto status = service->Status();

    EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
    EXPECT_FALSE(status.diagnostics.empty());
    EXPECT_GT(service->EstimatedPersistentBytesForTesting(), loaded.metadata->EstimatedBytes());
    EXPECT_LE(service->EstimatedPersistentBytesForTesting(), limits.maxPersistentBytes);
}

TEST(MetadataIndexServiceTest, DestructionCancelsAndJoinsTheWorker)
{
    auto loaded = LoadFixture();
    auto gate = std::make_shared<DecodeGate>();
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}},
        [gate](uint64_t offset, const CancellationToken &token) { return gate->Decode(offset, token); });
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(gate->WaitForEntered(1U));

    service.reset();

    EXPECT_TRUE(gate->WaitForCancellationExit());
}

TEST(MetadataIndexServiceTest, ObjectQueryFoldsAsciiOnlyAndEnforcesBounds)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}}, [](uint64_t offset, const CancellationToken &) {
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xB00U, "create"},
                                            {IdOperandKind::STRING, IdResolutionMode::INDEX, 2U, 0xB01U,
                                             "\xC3\x89"
                                             "clair"}});
        });
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery ascii;
    ascii.kind = MetadataKind::STRING;
    ascii.query = "CrEaTe";
    auto asciiPage = service->QueryObjects(ascii);
    ASSERT_TRUE(asciiPage.HasValue()) << asciiPage.Error().Message();
    EXPECT_NE(std::find_if(asciiPage.Value().items.begin(), asciiPage.Value().items.end(),
                           [](const auto &object) { return object.key.offset == 0xB00U; }),
              asciiPage.Value().items.end());

    MetadataObjectQuery exactUnicode;
    exactUnicode.kind = MetadataKind::STRING;
    exactUnicode.query =
        "\xC3\x89"
        "CLAIR";
    auto exactPage = service->QueryObjects(exactUnicode);
    ASSERT_TRUE(exactPage.HasValue()) << exactPage.Error().Message();
    ASSERT_EQ(exactPage.Value().items.size(), 1U);
    EXPECT_EQ(exactPage.Value().items.front().key.offset, 0xB01U);

    MetadataObjectQuery differentUnicode = exactUnicode;
    differentUnicode.query =
        "\xC3\xA9"
        "clair";
    auto differentPage = service->QueryObjects(differentUnicode);
    ASSERT_TRUE(differentPage.HasValue()) << differentPage.Error().Message();
    EXPECT_TRUE(differentPage.Value().items.empty());

    MetadataObjectQuery hardLimit;
    hardLimit.limit = 200U;
    EXPECT_TRUE(service->QueryObjects(hardLimit).HasValue());
    hardLimit.limit = 201U;
    auto oversizedPage = service->QueryObjects(hardLimit);
    ASSERT_FALSE(oversizedPage.HasValue());
    EXPECT_EQ(oversizedPage.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    MetadataObjectQuery oversizedQuery;
    oversizedQuery.query = std::string(257U, 'q');
    auto rejectedQuery = service->QueryObjects(oversizedQuery);
    ASSERT_FALSE(rejectedQuery.HasValue());
    EXPECT_EQ(rejectedQuery.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    oversizedQuery.query.resize(256U);
    EXPECT_TRUE(service->QueryObjects(oversizedQuery).HasValue());
}

TEST(MetadataIndexServiceTest, RevisionChangeMakesObjectCursorStale)
{
    auto loaded = LoadFixture();
    auto gate = std::make_shared<DecodeGate>();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x200U, 2U}},
        [gate](uint64_t offset, const CancellationToken &token) { return gate->Decode(offset, token); }, limits);
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(gate->WaitForEntered(1U));
    gate->Release(1U);
    ASSERT_TRUE(WaitUntil([service] { return service->Status().revision == 1U; }));

    MetadataObjectQuery query;
    query.limit = 1U;
    auto firstPage = service->QueryObjects(query);
    ASSERT_TRUE(firstPage.HasValue()) << firstPage.Error().Message();
    ASSERT_TRUE(firstPage.Value().nextCursor.has_value());
    query.cursor = firstPage.Value().nextCursor;
    ASSERT_TRUE(gate->WaitForEntered(2U));
    gate->Release(2U);
    service->WaitForIdleForTesting();

    auto stale = service->QueryObjects(query);
    ASSERT_FALSE(stale.HasValue());
    EXPECT_EQ(stale.Error().Code(), ErrorCode::CANCELLED);
    EXPECT_EQ(stale.Error().Message(), "Metadata cursor is stale; reload the first page");
}

TEST(MetadataIndexServiceTest, MatchingRevisionAndFilterPaginatesWithoutGapsOrDuplicates)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}}, [](uint64_t offset, const CancellationToken &) {
            std::vector<IdOperand> ids;
            for (uint32_t index = 0U; index < 5U; ++index) {
                ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, index, 0xC00U + index,
                                 "page-item-" + std::to_string(index));
            }
            return MakeDisassembly(offset, std::move(ids));
        });
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "PAGE-ITEM";
    query.limit = 2U;
    std::vector<MetadataObjectKey> keys;
    size_t total = 0U;
    do {
        auto page = service->QueryObjects(query);
        ASSERT_TRUE(page.HasValue()) << page.Error().Message();
        total = page.Value().total;
        for (const auto &item : page.Value().items) {
            keys.push_back(item.key);
        }
        query.cursor = page.Value().nextCursor;
    } while (query.cursor.has_value());

    EXPECT_EQ(total, 5U);
    EXPECT_EQ(keys.size(), 5U);
    for (size_t index = 0U; index < keys.size(); ++index) {
        EXPECT_EQ(keys[index], (MetadataObjectKey {MetadataKind::STRING, 0xC00U + index}));
    }
}

TEST(MetadataIndexServiceTest, CursorValidatesAllFieldsAndTheExactFilter)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();
    MetadataObjectQuery query;
    query.limit = 1U;
    auto first = service->QueryObjects(query);
    ASSERT_TRUE(first.HasValue());
    ASSERT_TRUE(first.Value().nextCursor.has_value());

    query.cursor = "m1:0:1";
    auto missingField = service->QueryObjects(query);
    ASSERT_FALSE(missingField.HasValue());
    EXPECT_EQ(missingField.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    query.cursor = first.Value().nextCursor;
    query.kind = MetadataKind::STRING;
    auto changedFilter = service->QueryObjects(query);
    ASSERT_FALSE(changedFilter.HasValue());
    EXPECT_EQ(changedFilter.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(MetadataIndexServiceTest, ReferencesAreBoundedAndPaginateDeterministically)
{
    auto loaded = LoadFixture();
    std::vector<MethodMetadata> methods;
    for (uint64_t index = 0U; index < 5U; ++index) {
        methods.push_back({0x100U + index, 1U});
    }
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, std::move(methods), [](uint64_t offset, const CancellationToken &) {
            return MakeDisassembly(offset,
                                   {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xD00U, "reference target"}});
        });
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    const MetadataObjectKey key {MetadataKind::STRING, 0xD00U};
    std::optional<std::string> cursor;
    std::vector<uint64_t> sources;
    size_t total = 0U;
    do {
        auto page = service->QueryReferences(key, cursor, 2U);
        ASSERT_TRUE(page.HasValue()) << page.Error().Message();
        total = page.Value().total;
        for (const auto &reference : page.Value().items) {
            ASSERT_TRUE(reference.sourceMethodOffset.has_value());
            sources.push_back(*reference.sourceMethodOffset);
        }
        cursor = page.Value().nextCursor;
    } while (cursor.has_value());
    EXPECT_EQ(total, 5U);
    EXPECT_EQ(sources, (std::vector<uint64_t> {0x100U, 0x101U, 0x102U, 0x103U, 0x104U}));

    MetadataObjectQuery objectQuery;
    objectQuery.kind = MetadataKind::STRING;
    objectQuery.query = "reference target";
    auto objectPage = service->QueryObjects(objectQuery);
    ASSERT_TRUE(objectPage.HasValue());
    ASSERT_EQ(objectPage.Value().items.size(), 1U);
    EXPECT_EQ(objectPage.Value().items.front().referenceCount, 5U);

    auto oversized = service->QueryReferences(key, std::nullopt, 201U);
    ASSERT_FALSE(oversized.HasValue());
    EXPECT_EQ(oversized.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(MetadataIndexServiceTest, ReferencesAtDifferentPcsAreNotDeduplicated)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}}, [](uint64_t methodOffset, const CancellationToken &) {
            auto disassembly = std::make_shared<FunctionDisassembly>();
            disassembly->methodOffset = methodOffset;
            for (uint32_t pc : {4U, 8U}) {
                InstructionInfo instruction;
                instruction.pc = pc;
                instruction.fileRange = {0U, 0U};
                instruction.mnemonic = "same";
                instruction.ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xE00U, "same target");
                disassembly->instructions.push_back(std::move(instruction));
            }
            return DisassemblyResult {DisassemblyStatus::SUCCESS, std::move(disassembly), {}};
        });
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    auto references = service->QueryReferences({MetadataKind::STRING, 0xE00U}, std::nullopt, 100U);

    ASSERT_TRUE(references.HasValue()) << references.Error().Message();
    ASSERT_EQ(references.Value().items.size(), 2U);
    EXPECT_EQ(references.Value().items[0].pc, 4U);
    EXPECT_EQ(references.Value().items[1].pc, 8U);
}

TEST(MetadataIndexServiceTest, StringDetailReadsFromTheDocumentOutsideTheStoreLock)
{
    auto loaded = LoadFixture();
    const auto found =
        std::find_if(loaded.metadata->Objects().begin(), loaded.metadata->Objects().end(), [](const auto &object) {
            return object.key.kind == MetadataKind::STRING && object.preview == "create";
        });
    ASSERT_NE(found, loaded.metadata->Objects().end());
    auto service = MetadataIndexService::Create(loaded.document, loaded.methods, loaded.metadata);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();
    EXPECT_EQ(service->Status().phase, MetadataIndexPhase::READY);

    auto detail = service->QueryObject(found->key);

    ASSERT_TRUE(detail.HasValue()) << detail.Error().Message();
    ASSERT_TRUE(detail.Value().value.has_value());
    EXPECT_EQ(*detail.Value().value, "create");
    EXPECT_LE(detail.Value().value->size(), MetadataLimits::MAX_DETAIL_BYTES);
}

TEST(MetadataIndexServiceTest, PartialBaseCatalogKeepsPartialTerminalState)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(document.HasValue());
    auto physical = PhysicalReader::Read(document.Value()->File());
    ASSERT_TRUE(physical.HasValue());
    auto structure = StructureClassifier::Build(document.Value()->Summary().path, physical.Value());
    ASSERT_TRUE(structure.HasValue());
    MetadataLimits catalogLimits;
    catalogLimits.maxPersistentBytes = 1U;
    auto catalog =
        MetadataCatalog::Build(document.Value()->File(), physical.Value(), *structure.Value(), catalogLimits);
    ASSERT_TRUE(catalog.HasValue());
    ASSERT_TRUE(catalog.Value()->IsPartial());
    MetadataIndexLimits indexLimits;
    indexLimits.maxPersistentBytes = MetadataIndexLimits::DEFAULT_MAX_PERSISTENT_BYTES;
    auto service = MetadataIndexService::CreateForTesting(catalog.Value(), {}, {}, indexLimits);

    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();
    const auto status = service->Status();

    EXPECT_EQ(status.phase, MetadataIndexPhase::PARTIAL);
    EXPECT_FALSE(status.diagnostics.empty());
}

TEST(MetadataIndexServiceTest, RejectsMalformedUtf8Queries)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);
    const std::vector<std::string> invalidQueries {"safe\x80", "safe\xC0\xAF", "safe\xED\xA0\x80", "safe\xE2\x82"};

    for (const auto &invalid : invalidQueries) {
        MetadataObjectQuery query;
        query.query = invalid;
        auto result = service->QueryObjects(query);
        ASSERT_FALSE(result.HasValue()) << "accepted malformed query with " << invalid.size() << " bytes";
        EXPECT_EQ(result.Error().Code(), ErrorCode::INVALID_ARGUMENT);
    }
}

TEST(MetadataIndexServiceTest, CompletePreviewReplacesTruncatedPreviewAsAPair)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x200U, 2U}},
        [](uint64_t offset, const CancellationToken &) {
            const auto preview =
                offset == 0x100U ? std::string(MetadataLimits::MAX_PREVIEW_BYTES + 1U, 'x') : std::string("complete");
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF00U, preview}});
        },
        limits);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "complete";
    auto page = service->QueryObjects(query);

    ASSERT_TRUE(page.HasValue()) << page.Error().Message();
    ASSERT_EQ(page.Value().items.size(), 1U);
    EXPECT_EQ(page.Value().items.front().preview, "complete");
    EXPECT_FALSE(page.Value().items.front().truncated);
}

TEST(MetadataIndexServiceTest, CanonicalPreviewSelectionMatchesTheObjectMergeRule)
{
    MetadataObject current;
    MetadataObject candidate;
    candidate.preview = "candidate";

    EXPECT_TRUE(ShouldReplaceMetadataObjectPreview(current, candidate));

    current.preview = "truncated";
    current.truncated = true;
    EXPECT_TRUE(ShouldReplaceMetadataObjectPreview(current, candidate));

    current.preview = "complete";
    current.truncated = false;
    candidate.truncated = true;
    EXPECT_FALSE(ShouldReplaceMetadataObjectPreview(current, candidate));
}

TEST(MetadataIndexServiceTest, SameBatchObjectFoldKeepsTheCompletePreviewVisibleToQueries)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.batchMethods = 2U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x200U, 2U}},
        [](uint64_t offset, const CancellationToken &) {
            const auto preview = offset == 0x100U ? std::string(MetadataLimits::MAX_PREVIEW_BYTES + 1U, 'x')
                                                  : std::string("same-batch-complete-preview");
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF08U, preview}});
        },
        limits);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "same-batch-complete-preview";
    auto page = service->QueryObjects(query);

    ASSERT_TRUE(page.HasValue()) << page.Error().Message();
    ASSERT_EQ(page.Value().items.size(), 1U);
    EXPECT_EQ(page.Value().items.front().preview, query.query);
    EXPECT_FALSE(page.Value().items.front().truncated);
}

TEST(MetadataIndexServiceTest, ObjectQueryFiltersTheFusedBaseAndOverlayPreview)
{
    auto loaded = LoadFixture();
    const auto base =
        std::find_if(loaded.metadata->Objects().begin(), loaded.metadata->Objects().end(), [](const auto &object) {
            return object.key.kind == MetadataKind::STRING && !object.preview.empty();
        });
    ASSERT_NE(base, loaded.metadata->Objects().end());
    const auto key = base->key;
    const_cast<MetadataObject &>(*base).truncated = true;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}}, [key](uint64_t offset, const CancellationToken &) {
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, key.offset,
                                             "task2-complete-overlay-preview"}});
        });
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "task2-complete-overlay-preview";
    auto page = service->QueryObjects(query);

    ASSERT_TRUE(page.HasValue()) << page.Error().Message();
    ASSERT_EQ(page.Value().items.size(), 1U);
    EXPECT_EQ(page.Value().items.front().key, key);
    EXPECT_EQ(page.Value().items.front().preview, query.query);
    EXPECT_FALSE(page.Value().items.front().truncated);
}

TEST(MetadataIndexServiceTest, LazyEvidenceEnrichesTheSameRealCatalogOperandWithoutDuplicatingIt)
{
    auto loaded = LoadFixture();
    const auto baseCandidate = std::find_if(loaded.metadata->StructuralReferences().begin(),
                                            loaded.metadata->StructuralReferences().end(), [](const auto &reference) {
                                                return reference.kind == MetadataReferenceKind::INSTRUCTION_OPERAND &&
                                                       reference.sourceMethodOffset.has_value() &&
                                                       HasReliableRange(reference);
                                            });
    ASSERT_NE(baseCandidate, loaded.metadata->StructuralReferences().end());
    const MetadataReference base = *baseCandidate;
    auto service = MetadataIndexService::Create(loaded.document, loaded.methods, loaded.metadata);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    auto references = service->QueryReferences(base.target, std::nullopt, 200U);
    ASSERT_TRUE(references.HasValue()) << references.Error().Message();
    const auto semanticCount =
        std::count_if(references.Value().items.begin(), references.Value().items.end(),
                      [base](const auto &candidate) { return SameSemanticSource(candidate, base); });
    const auto enriched =
        std::find_if(references.Value().items.begin(), references.Value().items.end(), [base](const auto &candidate) {
            return SameSemanticSource(candidate, base) && candidate.pc.has_value() && !candidate.mnemonic.empty();
        });

    EXPECT_EQ(semanticCount, 1U);
    ASSERT_NE(enriched, references.Value().items.end());
    EXPECT_FALSE(enriched->sourceName.empty());
    EXPECT_FALSE(enriched->sourceEntityIds.empty());

    MetadataObjectQuery query;
    query.limit = 200U;
    size_t visibleReferenceCount = 0U;
    do {
        auto objects = service->QueryObjects(query);
        ASSERT_TRUE(objects.HasValue()) << objects.Error().Message();
        for (const auto &object : objects.Value().items) {
            auto objectReferences = service->QueryReferences(object.key, std::nullopt, 200U);
            ASSERT_TRUE(objectReferences.HasValue()) << objectReferences.Error().Message();
            EXPECT_EQ(object.referenceCount, objectReferences.Value().total);
            visibleReferenceCount += objectReferences.Value().total;
        }
        query.cursor = objects.Value().nextCursor;
    } while (query.cursor.has_value());
    EXPECT_EQ(service->Status().referenceCount, visibleReferenceCount);
}

TEST(MetadataIndexServiceTest, LaterBatchEnrichesAnExistingOverlaySemanticSource)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x100U, 1U}},
        [decodeCalls = size_t {0U}](uint64_t offset, const CancellationToken &) mutable {
            ++decodeCalls;
            const auto preview =
                decodeCalls == 1U ? std::string(MetadataLimits::MAX_PREVIEW_BYTES + 1U, 'x') : std::string("complete");
            const auto mnemonic = decodeCalls == 1U ? std::string() : std::string("enriched");
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF10U, preview}},
                                   mnemonic);
        },
        limits);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    auto references = service->QueryReferences({MetadataKind::STRING, 0xF10U}, std::nullopt, 200U);
    ASSERT_TRUE(references.HasValue()) << references.Error().Message();
    ASSERT_EQ(references.Value().items.size(), 1U);
    EXPECT_EQ(references.Value().items.front().mnemonic, "enriched");

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "complete";
    auto objects = service->QueryObjects(query);
    ASSERT_TRUE(objects.HasValue()) << objects.Error().Message();
    ASSERT_EQ(objects.Value().items.size(), 1U);
    EXPECT_EQ(objects.Value().items.front().referenceCount, 1U);
    EXPECT_EQ(service->Status().referenceCount, loaded.metadata->StructuralReferences().size() + 1U);
}

TEST(MetadataIndexServiceTest, UnreliableRangesUseMethodAndPcAsTheirSemanticIdentity)
{
    auto loaded = LoadFixture();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}, {0x100U, 1U}},
        [decodeCalls = size_t {0U}](uint64_t methodOffset, const CancellationToken &) mutable {
            auto disassembly = std::make_shared<FunctionDisassembly>();
            disassembly->methodOffset = methodOffset;
            InstructionInfo instruction;
            instruction.pc = 9U;
            instruction.fileRange = {decodeCalls++ == 0U ? 0U : 0xABC0U, 0U};
            instruction.mnemonic = "unreliable";
            instruction.ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF18U,
                                         "same unreliable source");
            disassembly->instructions.push_back(std::move(instruction));
            return DisassemblyResult {DisassemblyStatus::SUCCESS, std::move(disassembly), {}};
        },
        limits);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    auto references = service->QueryReferences({MetadataKind::STRING, 0xF18U}, std::nullopt, 200U);

    ASSERT_TRUE(references.HasValue()) << references.Error().Message();
    ASSERT_EQ(references.Value().items.size(), 1U);
    EXPECT_EQ(references.Value().items.front().sourceMethodOffset, 0x100U);
    EXPECT_EQ(references.Value().items.front().pc, 9U);
}

TEST(MetadataIndexServiceTest, PersistentLimitAdmitsExactlyOneRetainedBatchDelta)
{
    auto loaded = LoadFixture();
    const auto decode = [](uint64_t offset, const CancellationToken &) {
        return MakeDisassembly(offset,
                               {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF20U, "one retained item"}});
    };
    auto measuring = MetadataIndexService::CreateForTesting(loaded.metadata, {{0x100U, 1U}}, decode);
    ASSERT_NE(measuring, nullptr);
    ASSERT_TRUE(measuring->Start());
    measuring->WaitForIdleForTesting();
    ASSERT_EQ(measuring->Status().phase, MetadataIndexPhase::READY);
    const auto retainedDelta = measuring->EstimatedPersistentBytesForTesting() - loaded.metadata->EstimatedBytes();
    ASSERT_GT(retainedDelta, 0U);

    MetadataIndexLimits limits;
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() + retainedDelta;
    auto exact = MetadataIndexService::CreateForTesting(loaded.metadata, {{0x100U, 1U}}, decode, limits);
    ASSERT_NE(exact, nullptr);
    ASSERT_TRUE(exact->Start());
    exact->WaitForIdleForTesting();

    EXPECT_EQ(exact->Status().phase, MetadataIndexPhase::READY);
    EXPECT_EQ(exact->EstimatedPersistentBytesForTesting(), limits.maxPersistentBytes);
}

TEST(MetadataIndexServiceTest, DuplicateHeavyBatchChargesOnlyOneSemanticReference)
{
    auto loaded = LoadFixture();
    const auto one = [](uint64_t methodOffset, const CancellationToken &) {
        auto disassembly = std::make_shared<FunctionDisassembly>();
        disassembly->methodOffset = methodOffset;
        InstructionInfo instruction;
        instruction.pc = 1U;
        instruction.fileRange = {0x1234U, 2U};
        instruction.mnemonic = "same";
        instruction.ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF30U, "same target");
        disassembly->instructions.push_back(std::move(instruction));
        return DisassemblyResult {DisassemblyStatus::SUCCESS, std::move(disassembly), {}};
    };
    auto measuring = MetadataIndexService::CreateForTesting(loaded.metadata, {{0x100U, 1U}}, one);
    ASSERT_NE(measuring, nullptr);
    ASSERT_TRUE(measuring->Start());
    measuring->WaitForIdleForTesting();
    ASSERT_EQ(measuring->Status().phase, MetadataIndexPhase::READY);
    const auto retainedDelta = measuring->EstimatedPersistentBytesForTesting() - loaded.metadata->EstimatedBytes();
    ASSERT_GT(retainedDelta, 0U);

    MetadataIndexLimits limits;
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() + retainedDelta;
    auto duplicateHeavy = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}},
        [](uint64_t methodOffset, const CancellationToken &) {
            auto disassembly = std::make_shared<FunctionDisassembly>();
            disassembly->methodOffset = methodOffset;
            for (uint32_t pc = 0U; pc < 200U; ++pc) {
                InstructionInfo instruction;
                instruction.pc = pc;
                instruction.fileRange = {0x1234U, 2U};
                instruction.mnemonic = "same";
                instruction.ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF30U, "same target");
                disassembly->instructions.push_back(std::move(instruction));
            }
            return DisassemblyResult {DisassemblyStatus::SUCCESS, std::move(disassembly), {}};
        },
        limits);
    ASSERT_NE(duplicateHeavy, nullptr);
    ASSERT_TRUE(duplicateHeavy->Start());
    duplicateHeavy->WaitForIdleForTesting();

    EXPECT_EQ(duplicateHeavy->Status().phase, MetadataIndexPhase::READY);
    EXPECT_LE(duplicateHeavy->EstimatedPersistentBytesForTesting(), limits.maxPersistentBytes);
    auto references = duplicateHeavy->QueryReferences({MetadataKind::STRING, 0xF30U}, std::nullopt, 200U);
    ASSERT_TRUE(references.HasValue()) << references.Error().Message();
    EXPECT_EQ(references.Value().total, 1U);
}

TEST(MetadataIndexServiceTest, PostFailureLeavesAOneShotFailedService)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);

    EXPECT_FALSE(MetadataIndexServiceTestAccess::StartWithBeforePostFailure(
        service, [] { throw std::runtime_error("post exploded"); }));
    EXPECT_FALSE(service->Start());
    service->WaitForIdleForTesting();
    const auto status = service->Status();
    EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
    ASSERT_EQ(status.diagnostics.size(), 1U);
    EXPECT_NE(status.diagnostics.front().find("post exploded"), std::string::npos);
}

TEST(MetadataIndexServiceTest, PostCallableConstructionStaysInsideTheStartBoundary)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);
    bool started = true;

    EXPECT_NO_THROW(started = MetadataIndexServiceTestAccess::StartWithThrowOnMovePost(service));

    EXPECT_FALSE(started);
    const auto status = service->Status();
    EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
    ASSERT_EQ(status.diagnostics.size(), 1U);
    EXPECT_NE(status.diagnostics.front().find("post callable invoked inside boundary"), std::string::npos);
}

TEST(MetadataIndexServiceTest, ThrowingPublishFinishesThePostedRunAsFailed)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}}, [](uint64_t offset, const CancellationToken &) {
            return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF40U, "publish"}});
        });
    ASSERT_NE(service, nullptr);

    EXPECT_TRUE(MetadataIndexServiceTestAccess::StartWithWorkerPublishFailure(
        service, [] { throw std::runtime_error("service publish exploded"); }));
    service->WaitForIdleForTesting();
    const auto status = service->Status();
    EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
    ASSERT_EQ(status.diagnostics.size(), 1U);
    EXPECT_NE(status.diagnostics.front().find("service publish exploded"), std::string::npos);
}

TEST(MetadataIndexServiceTest, BeforePostExceptionDiagnosticsBoundHugeAndInvalidUtf8What)
{
    auto loaded = LoadFixture();
    const auto messages = ExceptionDiagnosticMessages();
    for (size_t index = 0U; index < messages.size(); ++index) {
        auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
        ASSERT_NE(service, nullptr);

        EXPECT_FALSE(MetadataIndexServiceTestAccess::StartWithBeforePostFailure(
            service, [message = messages[index]] { throw std::runtime_error(message); }));
        service->WaitForIdleForTesting();
        const auto status = service->Status();
        EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
        ASSERT_EQ(status.diagnostics.size(), 1U);
        ExpectBoundedExceptionDiagnostic(status.diagnostics.front(),
                                         "Cannot schedule metadata reference index: ", index == 0U);
    }
}

TEST(MetadataIndexServiceTest, WorkerPublishExceptionDiagnosticsBoundHugeAndInvalidUtf8What)
{
    auto loaded = LoadFixture();
    const auto messages = ExceptionDiagnosticMessages();
    for (size_t index = 0U; index < messages.size(); ++index) {
        auto service = MetadataIndexService::CreateForTesting(
            loaded.metadata, {{0x100U, 1U}}, [](uint64_t offset, const CancellationToken &) {
                return MakeDisassembly(offset,
                                       {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0xF48U, "publish"}});
            });
        ASSERT_NE(service, nullptr);

        EXPECT_TRUE(MetadataIndexServiceTestAccess::StartWithWorkerPublishFailure(
            service, [message = messages[index]] { throw std::runtime_error(message); }));
        service->WaitForIdleForTesting();
        const auto status = service->Status();
        EXPECT_EQ(status.phase, MetadataIndexPhase::FAILED);
        ASSERT_EQ(status.diagnostics.size(), 1U);
        ExpectBoundedExceptionDiagnostic(status.diagnostics.front(),
                                         "Cannot publish metadata reference index: ", index == 0U);
    }
}

TEST(MetadataIndexServiceTest, ZeroLimitsAndMalformedCursorFieldsAreRejected)
{
    auto loaded = LoadFixture();
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    service->WaitForIdleForTesting();

    MetadataObjectQuery objectQuery;
    objectQuery.limit = 0U;
    auto zeroObjects = service->QueryObjects(objectQuery);
    ASSERT_FALSE(zeroObjects.HasValue());
    EXPECT_EQ(zeroObjects.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    const auto key = loaded.metadata->Objects().front().key;
    auto zeroReferences = service->QueryReferences(key, std::nullopt, 0U);
    ASSERT_FALSE(zeroReferences.HasValue());
    EXPECT_EQ(zeroReferences.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    objectQuery.limit = 1U;
    objectQuery.cursor = "m1:10000000000000000:1:0";
    auto overflow = service->QueryObjects(objectQuery);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_EQ(overflow.Error().Code(), ErrorCode::INVALID_ARGUMENT);

    objectQuery.cursor = "m1:0:1:0:extra";
    auto extra = service->QueryObjects(objectQuery);
    ASSERT_FALSE(extra.HasValue());
    EXPECT_EQ(extra.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(MetadataIndexServiceTest, ReferenceCursorUsesTheExactStaleError)
{
    auto loaded = LoadFixture();
    const auto key = std::find_if(loaded.metadata->Objects().begin(), loaded.metadata->Objects().end(),
                                  [](const auto &object) { return object.referenceCount >= 2U; });
    ASSERT_NE(key, loaded.metadata->Objects().end());
    auto gate = std::make_shared<DecodeGate>();
    MetadataIndexLimits limits;
    limits.batchMethods = 1U;
    limits.batchInterval = 1h;
    auto service = MetadataIndexService::CreateForTesting(
        loaded.metadata, {{0x100U, 1U}},
        [gate](uint64_t offset, const CancellationToken &token) { return gate->Decode(offset, token); }, limits);
    ASSERT_NE(service, nullptr);
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(gate->WaitForEntered(1U));

    auto first = service->QueryReferences(key->key, std::nullopt, 1U);
    ASSERT_TRUE(first.HasValue()) << first.Error().Message();
    ASSERT_TRUE(first.Value().nextCursor.has_value());
    gate->Release(1U);
    service->WaitForIdleForTesting();

    auto stale = service->QueryReferences(key->key, first.Value().nextCursor, 1U);
    ASSERT_FALSE(stale.HasValue());
    EXPECT_EQ(stale.Error().Code(), ErrorCode::CANCELLED);
    EXPECT_EQ(stale.Error().Message(), "Metadata cursor is stale; reload the first page");
}

TEST(MetadataIndexServiceTest, TestingFactoryReportsMissingDocumentForStringDetail)
{
    auto loaded = LoadFixture();
    const auto stringObject = std::find_if(loaded.metadata->Objects().begin(), loaded.metadata->Objects().end(),
                                           [](const auto &object) { return object.key.kind == MetadataKind::STRING; });
    ASSERT_NE(stringObject, loaded.metadata->Objects().end());
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {}, {});
    ASSERT_NE(service, nullptr);

    auto detail = service->QueryObject(stringObject->key);

    ASSERT_FALSE(detail.HasValue());
    EXPECT_EQ(detail.Error().Code(), ErrorCode::BACKEND_ERROR);
}

MetadataReferenceIndexer::DecodeMethod MakeAtomicBatchDecode(std::shared_ptr<std::atomic<bool>> entered,
                                                             std::shared_ptr<std::atomic<bool>> released)
{
    return [entered, released](uint64_t offset, const CancellationToken &cancellation) {
        entered->store(true, std::memory_order_release);
        while (!released->load(std::memory_order_acquire) && !cancellation.IsCancellationRequested()) {
            std::this_thread::yield();
        }
        std::vector<IdOperand> ids;
        ids.reserve(100U);
        for (uint32_t index = 0U; index < 100U; ++index) {
            ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, index, 0x10000000U + index,
                             "task2-atomic-batch-" + std::to_string(index));
        }
        return MakeDisassembly(offset, std::move(ids));
    };
}

void AwaitAtomicallyPublishedBatch(const std::shared_ptr<MetadataIndexService> &service,
                                   const MetadataObjectQuery &query, const MetadataIndexStatus &initial)
{
    ASSERT_TRUE(WaitUntil([service, query, initial] {
        const auto status = service->Status();
        if (status.revision == 0U) {
            EXPECT_EQ(status.objectCount, initial.objectCount);
            EXPECT_EQ(status.referenceCount, initial.referenceCount);
        } else {
            EXPECT_EQ(status.revision, 1U);
            EXPECT_EQ(status.objectCount, initial.objectCount + 100U);
            EXPECT_EQ(status.referenceCount, initial.referenceCount + 100U);
        }

        auto page = service->QueryObjects(query);
        EXPECT_TRUE(page.HasValue()) << page.Error().Message();
        if (page.HasValue()) {
            if (page.Value().revision == 0U) {
                EXPECT_EQ(page.Value().total, 0U);
            } else {
                EXPECT_EQ(page.Value().revision, 1U);
                EXPECT_EQ(page.Value().total, 100U);
            }
        }
        return status.phase == MetadataIndexPhase::READY;
    }));
}

TEST(MetadataIndexServiceTest, PublishedBatchStateIsObservedAtomically)
{
    auto loaded = LoadFixture();
    auto entered = std::make_shared<std::atomic<bool>>(false);
    auto released = std::make_shared<std::atomic<bool>>(false);
    auto service = MetadataIndexService::CreateForTesting(loaded.metadata, {{0x100U, 1U}},
                                                          MakeAtomicBatchDecode(entered, released));
    ASSERT_NE(service, nullptr);

    const auto initial = service->Status();
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(WaitUntil([entered] { return entered->load(std::memory_order_acquire); }));
    released->store(true, std::memory_order_release);

    MetadataObjectQuery query;
    query.kind = MetadataKind::STRING;
    query.query = "task2-atomic-batch-";
    query.limit = 128U;
    ASSERT_NO_FATAL_FAILURE(AwaitAtomicallyPublishedBatch(service, query, initial));
    service->WaitForIdleForTesting();

    const auto final = service->Status();
    EXPECT_EQ(final.revision, 1U);
    EXPECT_EQ(final.objectCount, initial.objectCount + 100U);
    EXPECT_EQ(final.referenceCount, initial.referenceCount + 100U);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
