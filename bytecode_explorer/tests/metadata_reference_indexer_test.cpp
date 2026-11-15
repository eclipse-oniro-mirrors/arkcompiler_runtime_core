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
#include "core/metadata/metadata_reference_indexer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <limits>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

using namespace std::chrono_literals;

struct PublishedIndex {
    std::vector<MetadataObject> objects;
    std::vector<MetadataReference> references;
    std::vector<std::string> diagnostics;
    size_t publishCalls {0U};
    size_t maxObjectsPerBatch {0U};
    size_t maxReferencesPerBatch {0U};
};

LoadedDocument LoadFixture()
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    EXPECT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    return loaded.HasValue() ? std::move(loaded.Value()) : LoadedDocument {};
}

DisassemblyResult MakeDisassembly(uint64_t methodOffset, std::vector<IdOperand> ids,
                                  std::atomic<size_t> *liveCount = nullptr, uint32_t pc = 7U,
                                  std::string mnemonic = "load.metadata")
{
    auto *raw = new FunctionDisassembly();
    raw->methodOffset = methodOffset;
    InstructionInfo instruction;
    instruction.pc = pc;
    instruction.fileRange = {0x500U + pc, 3U};
    instruction.mnemonic = std::move(mnemonic);
    for (auto &id : ids) {
        instruction.ids.push_back(std::move(id));
    }
    raw->instructions.push_back(std::move(instruction));
    if (liveCount != nullptr) {
        ++(*liveCount);
    }
    auto deleter = [liveCount](const FunctionDisassembly *value) {
        delete value;
        if (liveCount != nullptr) {
            --(*liveCount);
        }
    };
    return {DisassemblyStatus::SUCCESS, FunctionDisassemblyPtr(raw, std::move(deleter)), {}};
}

MetadataReferenceIndexer::PublishBatch Capture(PublishedIndex *published)
{
    return [published](MetadataIndexBatch batch) {
        ++published->publishCalls;
        published->maxObjectsPerBatch = std::max(published->maxObjectsPerBatch, batch.objects.size());
        published->maxReferencesPerBatch = std::max(published->maxReferencesPerBatch, batch.references.size());
        published->objects.insert(published->objects.end(), std::make_move_iterator(batch.objects.begin()),
                                  std::make_move_iterator(batch.objects.end()));
        published->references.insert(published->references.end(), std::make_move_iterator(batch.references.begin()),
                                     std::make_move_iterator(batch.references.end()));
        published->diagnostics.insert(published->diagnostics.end(), std::make_move_iterator(batch.diagnostics.begin()),
                                      std::make_move_iterator(batch.diagnostics.end()));
        return true;
    };
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

MetadataIndexLimits SingleBatchLimits()
{
    MetadataIndexLimits limits;
    limits.batchMethods = 32U;
    limits.batchInterval = 1h;
    return limits;
}

TEST(MethodIndexTest, EnumeratesValidatedMethodsInStableOffsetOrder)
{
    auto loaded = LoadFixture();
    ASSERT_NE(loaded.methods, nullptr);

    const auto &ordered = loaded.methods->MethodsInOffsetOrder();

    ASSERT_EQ(ordered.size(), loaded.methods->Size());
    EXPECT_TRUE(std::is_sorted(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
        return left.methodOffset < right.methodOffset;
    }));
    for (const auto &method : ordered) {
        ASSERT_NE(loaded.methods->Find(method.methodOffset), nullptr);
        EXPECT_EQ(loaded.methods->Find(method.methodOffset)->codeOffset, method.codeOffset);
    }
}

TEST(MetadataReferenceIndexerTest, DeduplicatesObjectsAndOrdersReferencesAcrossMethods)
{
    auto loaded = LoadFixture();
    ASSERT_NE(loaded.metadata, nullptr);
    auto liveDisassemblyCount = std::make_shared<std::atomic<size_t>>(0U);
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}, {0x200U, 2U}};
    auto decode = [liveDisassemblyCount](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, "shared"}},
                               liveDisassemblyCount.get());
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    EXPECT_EQ(result.scannedMethods, 2U);
    ASSERT_EQ(published.objects.size(), 1U);
    EXPECT_EQ(published.objects.front().key, (MetadataObjectKey {MetadataKind::STRING, 0x300U}));
    ASSERT_EQ(published.references.size(), 2U);
    EXPECT_LT(published.references[0].sourceMethodOffset, published.references[1].sourceMethodOffset);
    EXPECT_EQ(liveDisassemblyCount->load(), 0U);
}

TEST(MetadataReferenceIndexerTest, MapsMethodAndLiteralArrayIds)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset,
                               {{IdOperandKind::METHOD, IdResolutionMode::INDEX, 1U, 0x400U, "callee"},
                                {IdOperandKind::LITERAL_ARRAY, IdResolutionMode::DIRECT_OFFSET, 0x500U, 0x500U, ""}});
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    ASSERT_EQ(published.objects.size(), 2U);
    EXPECT_EQ(published.objects[0].key, (MetadataObjectKey {MetadataKind::METHOD, 0x400U}));
    EXPECT_EQ(published.objects[1].key, (MetadataObjectKey {MetadataKind::LITERAL_ARRAY, 0x500U}));
    ASSERT_EQ(published.references.size(), 2U);
    EXPECT_EQ(published.references[0].target.kind, MetadataKind::METHOD);
    EXPECT_EQ(published.references[1].target.kind, MetadataKind::LITERAL_ARRAY);
}

TEST(MetadataReferenceIndexerTest, DuplicateTargetOperandsRemainDistinctReferences)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, "same"},
                                              {IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, "same"}});
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    ASSERT_EQ(published.objects.size(), 1U);
    ASSERT_EQ(published.references.size(), 2U);
    EXPECT_EQ(published.references[0].operandIndex, 0U);
    EXPECT_EQ(published.references[1].operandIndex, 1U);
}

TEST(MetadataReferenceIndexerTest, SkipsUnknownAndUnresolvedIds)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset,
                               {{IdOperandKind::UNKNOWN, IdResolutionMode::DIRECT_OFFSET, 0x300U, 0x300U, "unknown"},
                                {IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, std::nullopt, "stale"},
                                {IdOperandKind::METHOD, IdResolutionMode::INDEX, 2U, std::nullopt, ""}});
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    EXPECT_TRUE(published.objects.empty());
    EXPECT_TRUE(published.references.empty());
}

TEST(MetadataReferenceIndexerTest, MalformedMethodMakesPartialAndScanningContinues)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    auto decoded = std::make_shared<std::vector<uint64_t>>();
    const std::vector<MethodMetadata> methods {{0x100U, 1U}, {0x200U, 2U}};
    auto decode = [decoded](uint64_t methodOffset, const CancellationToken &) {
        decoded->push_back(methodOffset);
        if (methodOffset == 0x100U) {
            return DisassemblyResult {DisassemblyStatus::MALFORMED_BYTECODE, nullptr, "bad method"};
        }
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, "after"}});
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::PARTIAL);
    EXPECT_EQ(result.scannedMethods, 2U);
    EXPECT_EQ(*decoded, (std::vector<uint64_t> {0x100U, 0x200U}));
    ASSERT_EQ(published.objects.size(), 1U);
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(MetadataReferenceIndexerTest, CancellationStopsBeforeAnotherDecode)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    CancellationSource cancellation;
    auto decodeCalls = std::make_shared<size_t>(0U);
    const std::vector<MethodMetadata> methods {{0x100U, 1U}, {0x200U, 2U}};
    auto decode = [cancellation, decodeCalls](uint64_t methodOffset, const CancellationToken &) {
        ++*decodeCalls;
        cancellation.Cancel();
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, "first"}});
    };

    const auto result = MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published),
                                                        SingleBatchLimits(), cancellation.Token());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::CANCELLED);
    EXPECT_EQ(result.scannedMethods, 1U);
    EXPECT_EQ(*decodeCalls, 1U);
}

TEST(MetadataReferenceIndexerTest, TruncatesPreviewAtAValidUtf8Boundary)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    const std::string preview = std::string(511U, 'a') + "\xE2\x82\xAC";
    auto decode = [preview](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U, preview}});
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    ASSERT_EQ(published.objects.size(), 1U);
    EXPECT_EQ(published.objects.front().preview, std::string(511U, 'a'));
    EXPECT_TRUE(published.objects.front().truncated);
}

TEST(MetadataReferenceIndexerTest, BuildResultDiagnosticsStayInsideTheRemainingSharedBudget)
{
    auto loaded = LoadFixture();
    std::vector<MethodMetadata> methods(100U);
    for (size_t index = 0U; index < methods.size(); ++index) {
        methods[index] = {0x100U + index, 1U};
    }
    MetadataIndexLimits limits;
    constexpr uint64_t diagnosticBudget = 1024U;
    ASSERT_LE(loaded.metadata->EstimatedBytes(), std::numeric_limits<uint64_t>::max() - diagnosticBudget);
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() + diagnosticBudget;
    limits.batchMethods = 1U;
    auto decode = [](uint64_t, const CancellationToken &) {
        return DisassemblyResult {DisassemblyStatus::MALFORMED_BYTECODE, nullptr,
                                  "malformed method with a retained diagnostic"};
    };

    const auto result = MetadataReferenceIndexer::Build(
        *loaded.metadata, methods, decode, [](MetadataIndexBatch) { return true; }, limits);

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::PARTIAL);
    EXPECT_LT(result.scannedMethods, methods.size());
    uint64_t retainedBytes = 0U;
    for (const auto &diagnostic : result.diagnostics) {
        retainedBytes += 2U * sizeof(std::string) + 64U + 32U + diagnostic.capacity();
    }
    EXPECT_LE(retainedBytes, diagnosticBudget);
}

TEST(MetadataReferenceIndexerTest, CancellationIsPolledWhileExtractingOneLargeMethod)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        std::vector<IdOperand> ids;
        for (uint32_t index = 0U; index < 1000U; ++index) {
            ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, index, 0x1000U + index, "large");
        }
        return MakeDisassembly(methodOffset, std::move(ids));
    };
    auto cancellationChecks = std::make_shared<size_t>(0U);
    CancellationToken cancellation([cancellationChecks] { return ++*cancellationChecks >= 180U; });

    const auto result = MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published),
                                                        SingleBatchLimits(), cancellation);

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::CANCELLED);
    EXPECT_GE(*cancellationChecks, 180U);
    EXPECT_GT(published.objects.size(), 0U);
    EXPECT_LT(published.objects.size(), 1000U);
    EXPECT_EQ(published.objects.size(), published.references.size());
}

TEST(MetadataReferenceIndexerTest, OneLargeMethodIsPublishedInBoundedEntryBatches)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        std::vector<IdOperand> ids;
        for (uint32_t index = 0U; index < 1000U; ++index) {
            ids.emplace_back(IdOperandKind::STRING, IdResolutionMode::INDEX, index, 0x2000U + index, "budgeted item");
        }
        return MakeDisassembly(methodOffset, std::move(ids));
    };
    MetadataIndexLimits limits = SingleBatchLimits();
    constexpr uint64_t remainingBudget = 4096U;
    ASSERT_LE(loaded.metadata->EstimatedBytes(), std::numeric_limits<uint64_t>::max() - remainingBudget);
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() + remainingBudget;

    const auto result = MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), limits);

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    EXPECT_EQ(published.objects.size(), 1000U);
    EXPECT_EQ(published.references.size(), 1000U);
    EXPECT_GT(published.publishCalls, 1U);
    EXPECT_GT(published.maxObjectsPerBatch, 0U);
    EXPECT_LE(published.maxObjectsPerBatch, 128U);
    EXPECT_LE(published.maxReferencesPerBatch, 128U);
}

const MetadataReference *FindReferenceWithMethodEntities(const MetadataCatalog &catalog)
{
    for (const auto &reference : catalog.StructuralReferences()) {
        if (reference.sourceMethodOffset.has_value() &&
            !catalog.EntitiesForMethod(*reference.sourceMethodOffset).empty()) {
            return &reference;
        }
    }
    return nullptr;
}

TEST(MetadataReferenceIndexerTest, OneOversizedEntryCannotBypassTheWorkingByteLimit)
{
    auto loaded = LoadFixture();
    const auto *source = FindReferenceWithMethodEntities(*loaded.metadata);
    ASSERT_NE(source, nullptr);
    const auto methodOffset = *source->sourceMethodOffset;
    auto &entities = const_cast<std::vector<EntityId> &>(loaded.metadata->EntitiesForMethod(methodOffset));
    entities.assign(40000U, 1U);
    PublishedIndex published;
    auto decode = [](uint64_t offset, const CancellationToken &) {
        return MakeDisassembly(offset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x2F00U, "oversized"}});
    };

    const auto result = MetadataReferenceIndexer::Build(*loaded.metadata, {{methodOffset, 1U}}, decode,
                                                        Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::PARTIAL);
    EXPECT_TRUE(published.objects.empty());
    EXPECT_TRUE(published.references.empty());
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(MetadataReferenceIndexerTest, SanitizesInvalidUtf8FromInjectedPreviewAndMnemonic)
{
    auto loaded = LoadFixture();
    PublishedIndex published;
    const std::vector<std::string> invalidSuffixes {"\x80", "\xC0\xAF", "\xED\xA0\x80", "\xE2\x82"};
    std::vector<MethodMetadata> methods;
    for (size_t index = 0U; index < invalidSuffixes.size(); ++index) {
        methods.push_back({0x100U + index, 1U});
    }
    auto decode = [invalidSuffixes](uint64_t methodOffset, const CancellationToken &) {
        const auto index = static_cast<size_t>(methodOffset - 0x100U);
        const auto text = std::string("safe") + invalidSuffixes[index];
        return MakeDisassembly(methodOffset,
                               {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x300U + index, text}}, nullptr,
                               7U, text);
    };

    const auto result =
        MetadataReferenceIndexer::Build(*loaded.metadata, methods, decode, Capture(&published), SingleBatchLimits());

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::READY);
    ASSERT_EQ(published.objects.size(), invalidSuffixes.size());
    ASSERT_EQ(published.references.size(), invalidSuffixes.size());
    for (const auto &object : published.objects) {
        EXPECT_EQ(object.preview, "safe");
        EXPECT_TRUE(object.truncated);
    }
    for (const auto &reference : published.references) {
        EXPECT_EQ(reference.mnemonic, "safe");
    }
}

TEST(MetadataReferenceIndexerTest, BaseCatalogOverBudgetStopsBeforeZeroMethods)
{
    auto loaded = LoadFixture();
    ASSERT_GT(loaded.metadata->EstimatedBytes(), 0U);
    MetadataIndexLimits limits = SingleBatchLimits();
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() - 1U;
    auto publishCalls = std::make_shared<size_t>(0U);

    const auto result = MetadataReferenceIndexer::Build(
        *loaded.metadata, {}, {},
        [publishCalls](MetadataIndexBatch) {
            ++*publishCalls;
            return true;
        },
        limits);

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::PARTIAL);
    EXPECT_EQ(result.scannedMethods, 0U);
    ASSERT_EQ(result.diagnostics.size(), 1U);
    EXPECT_EQ(result.diagnostics.front(), "Metadata reference index persistent budget was exhausted");
    EXPECT_EQ(*publishCalls, 0U);
}

TEST(MetadataReferenceIndexerTest, BaseCatalogOverBudgetStopsBeforeCodeLessMethods)
{
    auto loaded = LoadFixture();
    ASSERT_GT(loaded.metadata->EstimatedBytes(), 0U);
    MetadataIndexLimits limits = SingleBatchLimits();
    limits.maxPersistentBytes = loaded.metadata->EstimatedBytes() - 1U;
    auto decodeCalls = std::make_shared<size_t>(0U);
    auto publishCalls = std::make_shared<size_t>(0U);
    const std::vector<MethodMetadata> methods {{0x100U, std::nullopt}};

    const auto result = MetadataReferenceIndexer::Build(
        *loaded.metadata, methods,
        [decodeCalls](uint64_t, const CancellationToken &) {
            ++*decodeCalls;
            return DisassemblyResult {};
        },
        [publishCalls](MetadataIndexBatch) {
            ++*publishCalls;
            return true;
        },
        limits);

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::PARTIAL);
    EXPECT_EQ(result.scannedMethods, 0U);
    ASSERT_EQ(result.diagnostics.size(), 1U);
    EXPECT_EQ(result.diagnostics.front(), "Metadata reference index persistent budget was exhausted");
    EXPECT_EQ(*decodeCalls, 0U);
    EXPECT_EQ(*publishCalls, 0U);
}

TEST(MetadataReferenceIndexerTest, PublishExceptionReturnsFailedInsteadOfEscaping)
{
    auto loaded = LoadFixture();
    const std::vector<MethodMetadata> methods {{0x100U, 1U}};
    auto decode = [](uint64_t methodOffset, const CancellationToken &) {
        return MakeDisassembly(methodOffset, {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x400U, "publish"}});
    };
    MetadataIndexBuildResult result;

    EXPECT_NO_THROW(result = MetadataReferenceIndexer::Build(
                        *loaded.metadata, methods, decode,
                        [](MetadataIndexBatch) -> bool { throw std::runtime_error("publish exploded"); },
                        SingleBatchLimits()));

    EXPECT_EQ(result.status, MetadataIndexBuildStatus::FAILED);
    ASSERT_EQ(result.diagnostics.size(), 1U);
    EXPECT_NE(result.diagnostics.front().find("publish exploded"), std::string::npos);
}

TEST(MetadataReferenceIndexerTest, BoundedDiagnosticReadsOnlyAValidUtf8Prefix)
{
    const auto messages = ExceptionDiagnosticMessages();

    const auto huge = MakeBoundedMetadataDiagnostic("bounded: ", messages[0].c_str());
    const auto invalid = MakeBoundedMetadataDiagnostic("bounded: ", messages[1].c_str());

    ASSERT_NO_FATAL_FAILURE(ExpectBoundedExceptionDiagnostic(huge, "bounded: ", true));
    ASSERT_NO_FATAL_FAILURE(ExpectBoundedExceptionDiagnostic(invalid, "bounded: ", false));
}

TEST(MetadataReferenceIndexerTest, DecodeExceptionDiagnosticsBoundHugeAndInvalidUtf8What)
{
    auto loaded = LoadFixture();
    const auto messages = ExceptionDiagnosticMessages();
    for (size_t index = 0U; index < messages.size(); ++index) {
        const auto result = MetadataReferenceIndexer::Build(
            *loaded.metadata, {{0x100U, 1U}},
            [message = messages[index]](uint64_t, const CancellationToken &) -> DisassemblyResult {
                throw std::runtime_error(message);
            },
            [](MetadataIndexBatch) { return true; }, SingleBatchLimits());

        EXPECT_EQ(result.status, MetadataIndexBuildStatus::FAILED);
        ASSERT_EQ(result.diagnostics.size(), 1U);
        ExpectBoundedExceptionDiagnostic(result.diagnostics.front(),
                                         "Cannot index METHOD at offset 256: ", index == 0U);
    }
}

TEST(MetadataReferenceIndexerTest, PublishExceptionDiagnosticsBoundHugeAndInvalidUtf8What)
{
    auto loaded = LoadFixture();
    const auto messages = ExceptionDiagnosticMessages();
    for (size_t index = 0U; index < messages.size(); ++index) {
        auto decode = [](uint64_t methodOffset, const CancellationToken &) {
            return MakeDisassembly(methodOffset,
                                   {{IdOperandKind::STRING, IdResolutionMode::INDEX, 1U, 0x410U, "publish"}});
        };
        const auto result = MetadataReferenceIndexer::Build(
            *loaded.metadata, {{0x100U, 1U}}, decode,
            [message = messages[index]](MetadataIndexBatch) -> bool { throw std::runtime_error(message); },
            SingleBatchLimits());

        EXPECT_EQ(result.status, MetadataIndexBuildStatus::FAILED);
        ASSERT_EQ(result.diagnostics.size(), 1U);
        ExpectBoundedExceptionDiagnostic(result.diagnostics.front(),
                                         "Cannot publish metadata reference index: ", index == 0U);
    }
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
