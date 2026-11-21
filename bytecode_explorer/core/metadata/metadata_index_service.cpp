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

#include "core/metadata/metadata_index_service.h"
#include "core/metadata/metadata_model.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

constexpr size_t MAX_PAGE_SIZE = 200U;
constexpr size_t MAX_QUERY_BYTES = 256U;
constexpr size_t MAX_DIAGNOSTICS = 64U;
constexpr size_t MAX_CURSOR_BYTES = 80U;
constexpr uint64_t MAP_NODE_OVERHEAD = 64U;
constexpr std::string_view STALE_CURSOR_MESSAGE = "Metadata cursor is stale; reload the first page";
constexpr std::string_view BUDGET_DIAGNOSTIC = "Metadata reference index persistent budget was exhausted";
constexpr uint64_t FNV1A_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV1A_PRIME = 1099511628211ULL;

bool IsValidUtf8(std::string_view value)
{
    size_t offset = 0U;
    while (offset < value.size()) {
        const auto first = static_cast<uint8_t>(value[offset]);
        const auto length = metadata_model_detail::Utf8SequenceLength(first);
        if (length == 0U || length > value.size() - offset) {
            return false;
        }
        const auto *sequence = reinterpret_cast<const uint8_t *>(value.data()) + offset;
        if (!metadata_model_detail::IsValidUtf8Sequence(sequence, length)) {
            return false;
        }
        offset += length;
    }
    return true;
}

uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
    return right > std::numeric_limits<uint64_t>::max() - left ? std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t EstimateString(const std::string &value)
{
    return SaturatingAdd(32U, value.capacity());
}

uint64_t EstimateObject(const MetadataObject &object)
{
    uint64_t bytes = sizeof(MetadataObjectKey) + sizeof(MetadataObject) + MAP_NODE_OVERHEAD;
    bytes = SaturatingAdd(bytes, EstimateString(object.preview));
    bytes = SaturatingAdd(bytes, object.origins.capacity() * sizeof(MetadataOrigin));
    bytes = SaturatingAdd(bytes, object.entityIds.capacity() * sizeof(EntityId));
    bytes = SaturatingAdd(bytes, object.diagnostics.capacity() * sizeof(std::string));
    for (const auto &diagnostic : object.diagnostics) {
        bytes = SaturatingAdd(bytes, EstimateString(diagnostic));
    }
    return bytes;
}

uint64_t EstimateReference(const MetadataReference &reference)
{
    uint64_t bytes = sizeof(MetadataReference) + MAP_NODE_OVERHEAD;
    bytes = SaturatingAdd(bytes, reference.sourceEntityIds.capacity() * sizeof(EntityId));
    bytes = SaturatingAdd(bytes, EstimateString(reference.sourceName));
    bytes = SaturatingAdd(bytes, EstimateString(reference.mnemonic));
    return bytes;
}

uint64_t EstimateDiagnostic(const std::string &diagnostic)
{
    return SaturatingAdd(sizeof(std::string) + 64U, EstimateString(diagnostic));
}

bool ContainsDiagnostic(const std::vector<std::string> &diagnostics, const std::string &diagnostic)
{
    return std::find(diagnostics.begin(), diagnostics.end(), diagnostic) != diagnostics.end();
}

void StoreDiagnostic(std::string diagnostic, uint64_t limit, uint64_t *estimatedBytes, bool *budgetExhausted,
                     std::vector<std::string> *diagnostics)
{
    if (diagnostics->size() >= MAX_DIAGNOSTICS || ContainsDiagnostic(*diagnostics, diagnostic)) {
        return;
    }
    const auto bytes = EstimateDiagnostic(diagnostic);
    if (bytes > limit || *estimatedBytes > limit - bytes) {
        *budgetExhausted = true;
        return;
    }
    diagnostics->push_back(std::move(diagnostic));
    *estimatedBytes += bytes;
}

uint64_t ReplaceEstimate(uint64_t total, uint64_t oldBytes, uint64_t newBytes)
{
    total = oldBytes > total ? 0U : total - oldBytes;
    return SaturatingAdd(total, newBytes);
}

bool HasReliableRange(const MetadataReference &reference)
{
    return reference.sourceRange.has_value() && reference.sourceRange->size != 0U;
}

uint64_t ReferenceSource(const MetadataReference &reference)
{
    return reference.sourceRange.has_value() ? reference.sourceRange->offset
                                             : reference.sourceMethodOffset.value_or(0U);
}

uint64_t SemanticReferenceSource(const MetadataReference &reference)
{
    return HasReliableRange(reference) ? reference.sourceRange->offset : reference.sourceMethodOffset.value_or(0U);
}

bool ReferenceSourcePrefixLess(const MetadataReference &left, const MetadataReference &right)
{
    const auto leftSource = ReferenceSource(left);
    const auto rightSource = ReferenceSource(right);
    return std::tie(left.target, left.kind, leftSource, left.operandIndex) <
           std::tie(right.target, right.kind, rightSource, right.operandIndex);
}

bool ReferenceSemanticLess(const MetadataReference &left, const MetadataReference &right) noexcept
{
    const auto leftSource = SemanticReferenceSource(left);
    const auto rightSource = SemanticReferenceSource(right);
    const bool leftHasRange = HasReliableRange(left);
    const bool rightHasRange = HasReliableRange(right);
    const uint64_t leftRangeOffset = leftHasRange ? left.sourceRange->offset : 0U;
    const uint64_t rightRangeOffset = rightHasRange ? right.sourceRange->offset : 0U;
    const uint64_t leftRangeSize = leftHasRange ? left.sourceRange->size : 0U;
    const uint64_t rightRangeSize = rightHasRange ? right.sourceRange->size : 0U;
    const std::optional<uint32_t> leftPc = leftHasRange ? std::nullopt : left.pc;
    const std::optional<uint32_t> rightPc = rightHasRange ? std::nullopt : right.pc;
    return std::tie(left.target, left.kind, leftSource, left.operandIndex, left.sourceMethodOffset, leftHasRange,
                    leftRangeOffset, leftRangeSize, leftPc) <
           std::tie(right.target, right.kind, rightSource, right.operandIndex, right.sourceMethodOffset, rightHasRange,
                    rightRangeOffset, rightRangeSize, rightPc);
}

bool ReferenceSemanticEqual(const MetadataReference &left, const MetadataReference &right) noexcept
{
    return !ReferenceSemanticLess(left, right) && !ReferenceSemanticLess(right, left);
}

const MetadataReference *FindBaseSemanticReference(const std::vector<MetadataReference> &base,
                                                   const MetadataReference &reference)
{
    const auto range = std::equal_range(base.begin(), base.end(), reference, ReferenceSourcePrefixLess);
    const auto found = std::find_if(range.first, range.second, [&reference](const auto &candidate) {
        return ReferenceSemanticEqual(candidate, reference);
    });
    return found == range.second ? nullptr : &*found;
}

template <class T>
void AppendMissing(std::vector<T> *target, const std::vector<T> &source)
{
    for (const auto &value : source) {
        if (std::find(target->begin(), target->end(), value) == target->end()) {
            target->push_back(value);
        }
    }
    std::sort(target->begin(), target->end());
}

void MergeReference(MetadataReference *target, const MetadataReference &source)
{
    AppendMissing(&target->sourceEntityIds, source.sourceEntityIds);
    if (!target->sourceMethodOffset.has_value()) {
        target->sourceMethodOffset = source.sourceMethodOffset;
    }
    if (!target->pc.has_value()) {
        target->pc = source.pc;
    }
    if (!target->operandIndex.has_value()) {
        target->operandIndex = source.operandIndex;
    }
    if (!HasReliableRange(*target) && HasReliableRange(source)) {
        target->sourceRange = source.sourceRange;
    }
    if (target->sourceName.empty()) {
        target->sourceName = source.sourceName;
    }
    if (target->mnemonic.empty()) {
        target->mnemonic = source.mnemonic;
    }
}

char FoldAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool ContainsAsciiInsensitive(std::string_view text, std::string_view query)
{
    if (query.empty()) {
        return true;
    }
    if (query.size() > text.size()) {
        return false;
    }
    for (size_t start = 0U; start <= text.size() - query.size(); ++start) {
        bool matches = true;
        for (size_t index = 0U; index < query.size(); ++index) {
            if (FoldAscii(text[start + index]) != FoldAscii(query[index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

uint64_t HashByte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * FNV1A_PRIME;
}

uint64_t HashText(uint64_t hash, std::string_view text)
{
    for (const auto value : text) {
        hash = HashByte(hash, static_cast<uint8_t>(value));
    }
    return hash;
}

uint64_t ObjectFilterHash(const MetadataObjectQuery &query)
{
    auto hash = HashText(FNV1A_OFFSET, "objects");
    hash = HashByte(hash, query.kind.has_value() ? static_cast<uint8_t>(*query.kind) + 1U : 0U);
    return HashText(hash, query.query);
}

uint64_t ReferenceFilterHash(const MetadataObjectKey &key)
{
    auto hash = HashText(FNV1A_OFFSET, "references");
    hash = HashByte(hash, static_cast<uint8_t>(key.kind));
    for (size_t shift = 0U; shift < sizeof(key.offset); ++shift) {
        hash = HashByte(hash, static_cast<uint8_t>(key.offset >> (shift * 8U)));
    }
    return hash;
}

std::string MakeCursor(uint64_t revision, size_t nextIndex, uint64_t filterHash)
{
    std::ostringstream cursor;
    cursor << "m1:" << std::hex << revision << ':' << nextIndex << ':' << filterHash;
    return cursor.str();
}

bool ParseHex(std::string_view text, uint64_t *value)
{
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value, 16);
    return parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size();
}

Result<size_t> ParseCursor(const std::optional<std::string> &cursor, uint64_t revision, uint64_t filterHash)
{
    if (!cursor.has_value()) {
        return Result<size_t>::Success(0U);
    }
    const std::string_view text = *cursor;
    if (text.size() > MAX_CURSOR_BYTES || text.substr(0U, 3U) != "m1:") {
        return Result<size_t>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "Metadata cursor is invalid"));
    }
    const auto revisionEnd = text.find(':', 3U);
    const auto indexEnd = revisionEnd == std::string_view::npos ? revisionEnd : text.find(':', revisionEnd + 1U);
    if (revisionEnd == std::string_view::npos || indexEnd == std::string_view::npos ||
        text.find(':', indexEnd + 1U) != std::string_view::npos) {
        return Result<size_t>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "Metadata cursor is invalid"));
    }
    uint64_t parsedRevision = 0U;
    uint64_t parsedIndex = 0U;
    uint64_t parsedHash = 0U;
    if (!ParseHex(text.substr(3U, revisionEnd - 3U), &parsedRevision) ||
        !ParseHex(text.substr(revisionEnd + 1U, indexEnd - revisionEnd - 1U), &parsedIndex) ||
        !ParseHex(text.substr(indexEnd + 1U), &parsedHash) || parsedIndex > std::numeric_limits<size_t>::max() ||
        parsedHash != filterHash) {
        return Result<size_t>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "Metadata cursor is invalid"));
    }
    if (parsedRevision != revision) {
        return Result<size_t>::Failure(Status(ErrorCode::CANCELLED, std::string(STALE_CURSOR_MESSAGE)));
    }
    return Result<size_t>::Success(static_cast<size_t>(parsedIndex));
}

Status InvalidLimit()
{
    return Status(ErrorCode::INVALID_ARGUMENT, "Metadata page limit must be between 1 and 200");
}

static_assert(noexcept(std::swap(std::declval<MetadataObject &>(), std::declval<MetadataObject &>())));
static_assert(noexcept(std::swap(std::declval<MetadataReference &>(), std::declval<MetadataReference &>())));
static_assert(noexcept(std::declval<std::shared_ptr<const std::vector<std::string>> &>().swap(
    std::declval<std::shared_ptr<const std::vector<std::string>> &>())));

// Pairing of one staged object with its currently published counterpart.
struct ObjectContext {
    MetadataObject *staged {nullptr};
    const MetadataObject *current {nullptr};
    bool addsVisibleObject {false};
};

// Pairing of one staged reference with its currently published counterpart and with the
// semantic base entry it overrides, when any.
struct ReferenceContext {
    MetadataReference *staged {nullptr};
    const MetadataReference *current {nullptr};
    const MetadataReference *base {nullptr};
};

// Publish state prepared from the published snapshot plus the staged batch, applied
// atomically under the unique lock when the revision still matches.
struct PublishPreparation {
    size_t objectCount {0U};
    size_t referenceCount {0U};
    size_t scannedMethods {0U};
    uint64_t estimatedBytes {0U};
    uint64_t expectedRevision {0U};
    std::shared_ptr<const std::vector<std::string>> diagnostics;
};

// Maps a build status to the published phase, treating a partial catalog as PARTIAL.
MetadataIndexPhase PhaseForBuildResult(MetadataIndexBuildStatus status, bool catalogPartial)
{
    switch (status) {
        case MetadataIndexBuildStatus::READY:
            return catalogPartial ? MetadataIndexPhase::PARTIAL : MetadataIndexPhase::READY;
        case MetadataIndexBuildStatus::PARTIAL:
            return MetadataIndexPhase::PARTIAL;
        case MetadataIndexBuildStatus::CANCELLED:
            return MetadataIndexPhase::CANCELLED;
        case MetadataIndexBuildStatus::FAILED:
            return MetadataIndexPhase::FAILED;
    }
    return MetadataIndexPhase::FAILED;
}

// Merges the build result diagnostics into a private copy of the published diagnostics
// while the persistent budget allows, reporting exhaustion.
std::shared_ptr<const std::vector<std::string>> PrepareRunDiagnostics(
    const MetadataIndexBuildResult &result, const std::shared_ptr<const std::vector<std::string>> &previousDiagnostics,
    uint64_t maxPersistentBytes, uint64_t *preparedEstimatedBytes, bool *preparedBudgetExhausted)
{
    auto preparedDiagnostics = previousDiagnostics;
    std::shared_ptr<std::vector<std::string>> writableDiagnostics;
    if (!result.diagnostics.empty()) {
        writableDiagnostics = std::make_shared<std::vector<std::string>>(*previousDiagnostics);
    }
    for (auto &diagnostic : result.diagnostics) {
        StoreDiagnostic(std::move(diagnostic), maxPersistentBytes, preparedEstimatedBytes, preparedBudgetExhausted,
                        writableDiagnostics.get());
    }
    if (writableDiagnostics != nullptr) {
        preparedDiagnostics = std::move(writableDiagnostics);
    }
    return preparedDiagnostics;
}

// Stages one batch's keyed entries, merging duplicates into the earlier entry. The
// caller supplies the key derivation so staged maps with private key types stay local
// to the member function.
template <typename BatchVector, typename StagedMap, typename MakeKey, typename Merger>
void StageBatchEntries(BatchVector &entries, StagedMap *staged, MakeKey makeKey, Merger merge)
{
    for (auto &entry : entries) {
        auto key = makeKey(entry);
        auto [found, inserted] = staged->try_emplace(std::move(key), std::move(entry));
        if (!inserted) {
            merge(&found->second, entry);
        }
    }
}

// Pairs every staged entry with its currently published counterpart, if any.
template <typename StagedMap, typename DynamicMap, typename Context>
void CollectStagedContexts(StagedMap &staged, const DynamicMap &dynamic, std::vector<Context> *contexts)
{
    contexts->reserve(staged.size());
    for (auto &[key, value] : staged) {
        const auto current = dynamic.find(key);
        contexts->push_back({&value, current == dynamic.end() ? nullptr : &current->second});
    }
}

// Computes which staged objects introduce a visible object and which staged references override a
// semantic base entry of the catalog.
void ComputePublishVisibility(const MetadataCatalog &catalog, std::vector<ObjectContext> *objectContexts,
                              std::vector<ReferenceContext> *referenceContexts)
{
    for (auto &context : *objectContexts) {
        context.addsVisibleObject = context.current == nullptr && catalog.Find(context.staged->key) == nullptr;
    }
    for (auto &context : *referenceContexts) {
        context.base = FindBaseSemanticReference(catalog.StructuralReferences(), *context.staged);
    }
}

// Merges the published counterpart or the semantic base into every staged entry, tallying the newly
// visible objects and references and replacing the affected byte estimates in the preparation.
template <typename AddedCountsMap>
void ApplyStagedMerges(const std::vector<ObjectContext> &objectContexts,
                       const std::vector<ReferenceContext> &referenceContexts, AddedCountsMap *addedReferenceCounts,
                       uint64_t referenceOverhead, PublishPreparation *preparation)
{
    for (auto &context : referenceContexts) {
        if (context.current != nullptr) {
            MergeReference(context.staged, *context.current);
        } else if (context.base != nullptr) {
            MergeReference(context.staged, *context.base);
        } else {
            ++preparation->referenceCount;
            ++(*addedReferenceCounts)[context.staged->target];
        }
    }
    for (auto &context : objectContexts) {
        if (context.current != nullptr) {
            MergeMetadataObjectEvidence(context.staged, *context.current);
        } else if (context.addsVisibleObject) {
            ++preparation->objectCount;
        }
        if (const auto found = addedReferenceCounts->find(context.staged->key); found != addedReferenceCounts->end()) {
            context.staged->referenceCount += found->second;
        }
        preparation->estimatedBytes = ReplaceEstimate(
            preparation->estimatedBytes, context.current == nullptr ? 0U : EstimateObject(*context.current),
            EstimateObject(*context.staged));
    }
    for (const auto &context : referenceContexts) {
        const auto oldBytes = context.current == nullptr ? 0U : referenceOverhead + EstimateReference(*context.current);
        const auto newBytes = referenceOverhead + EstimateReference(*context.staged);
        preparation->estimatedBytes = ReplaceEstimate(preparation->estimatedBytes, oldBytes, newBytes);
    }
}

// Appends the batch diagnostics to a private copy of the published diagnostics, dropping duplicates
// and entries past the diagnostics cap while folding their estimated bytes into the preparation.
std::shared_ptr<const std::vector<std::string>> MergeBatchDiagnostics(
    std::vector<std::string> batchDiagnostics,
    const std::shared_ptr<const std::vector<std::string>> &previousDiagnostics, uint64_t *estimatedBytes)
{
    if (batchDiagnostics.empty()) {
        return previousDiagnostics;
    }
    auto writableDiagnostics = std::make_shared<std::vector<std::string>>(*previousDiagnostics);
    for (auto &diagnostic : batchDiagnostics) {
        if (writableDiagnostics->size() >= MAX_DIAGNOSTICS || ContainsDiagnostic(*writableDiagnostics, diagnostic)) {
            continue;
        }
        *estimatedBytes = SaturatingAdd(*estimatedBytes, EstimateDiagnostic(diagnostic));
        writableDiagnostics->push_back(std::move(diagnostic));
    }
    return writableDiagnostics;
}

// Moves every staged entry into the published map, swapping into the value of an existing entry.
template <typename StagedMap, typename DynamicMap>
void MergeStagedIntoDynamic(StagedMap &staged, DynamicMap *dynamic)
{
    while (!staged.empty()) {
        auto node = staged.extract(staged.begin());
        const auto current = dynamic->find(node.key());
        if (current == dynamic->end()) {
            dynamic->insert(std::move(node));
        } else {
            using std::swap;
            swap(current->second, node.mapped());
        }
    }
}

// Reports whether a candidate object matches the query's kind filter and case-insensitive text.
bool MatchesObjectQuery(const MetadataObjectKey &key, const std::string &preview, const MetadataObjectQuery &query)
{
    if (query.kind.has_value() && key.kind != *query.kind) {
        return false;
    }
    return ContainsAsciiInsensitive(preview, query.query);
}

// Appends the merged object to the page when it matches the query and the page window allows it,
// always advancing the page total.
void AppendObjectPageItem(const MetadataObject *baseObject, const MetadataObject *dynamicObject,
                          const MetadataObjectQuery &query, size_t startIndex, MetadataPage<MetadataObject> *page)
{
    const auto &key = baseObject != nullptr ? baseObject->key : dynamicObject->key;
    const auto *previewObject =
        baseObject == nullptr ||
                (dynamicObject != nullptr && ShouldReplaceMetadataObjectPreview(*baseObject, *dynamicObject))
            ? dynamicObject
            : baseObject;
    if (!MatchesObjectQuery(key, previewObject->preview, query)) {
        return;
    }
    if (page->total >= startIndex && page->items.size() < query.limit) {
        MetadataObject object = baseObject != nullptr ? *baseObject : *dynamicObject;
        if (baseObject != nullptr && dynamicObject != nullptr) {
            MergeMetadataObjectEvidence(&object, *dynamicObject);
        }
        page->items.push_back(std::move(object));
    }
    ++page->total;
}

}  // namespace

bool MetadataIndexService::ObjectKeyLess::operator()(const MetadataObjectKey &left,
                                                     const MetadataObjectKey &right) const noexcept
{
    return left.kind < right.kind || (left.kind == right.kind && left.offset < right.offset);
}

bool MetadataIndexService::ReferenceSourceKeyLess::operator()(const ReferenceSourceKey &left,
                                                              const ReferenceSourceKey &right) const noexcept
{
    return std::tie(left.target.kind, left.target.offset, left.kind, left.sourceOrder, left.operandIndex,
                    left.sourceMethodOffset, left.hasReliableRange, left.rangeOffset, left.rangeSize, left.pc) <
           std::tie(right.target.kind, right.target.offset, right.kind, right.sourceOrder, right.operandIndex,
                    right.sourceMethodOffset, right.hasReliableRange, right.rangeOffset, right.rangeSize, right.pc);
}

bool MetadataIndexService::ReferenceSourceKeyLess::operator()(const ReferenceSourceKey &left,
                                                              const MetadataObjectKey &right) const noexcept
{
    return left.target < right;
}

bool MetadataIndexService::ReferenceSourceKeyLess::operator()(const MetadataObjectKey &left,
                                                              const ReferenceSourceKey &right) const noexcept
{
    return left < right.target;
}

MetadataIndexService::ReferenceSourceKey MetadataIndexService::MakeReferenceSourceKey(
    const MetadataReference &reference) noexcept
{
    ReferenceSourceKey key;
    key.target = reference.target;
    key.kind = reference.kind;
    key.sourceOrder = SemanticReferenceSource(reference);
    key.sourceMethodOffset = reference.sourceMethodOffset;
    key.operandIndex = reference.operandIndex;
    key.hasReliableRange = HasReliableRange(reference);
    if (key.hasReliableRange) {
        key.rangeOffset = reference.sourceRange->offset;
        key.rangeSize = reference.sourceRange->size;
    } else {
        key.pc = reference.pc;
    }
    return key;
}

MetadataIndexService::MetadataIndexService(ConstructorToken, std::shared_ptr<const AbcDocument> document,
                                           std::shared_ptr<const MethodIndex> methodIndex,
                                           std::shared_ptr<const MetadataCatalog> catalog,
                                           std::vector<MethodMetadata> methods,
                                           MetadataReferenceIndexer::DecodeMethod decode, MetadataIndexLimits limits)
    : document_(std::move(document)),
      methodIndex_(std::move(methodIndex)),
      catalog_(std::move(catalog)),
      methods_(std::move(methods)),
      decode_(std::move(decode)),
      limits_(limits),
      objectCount_(catalog_->Objects().size()),
      referenceCount_(catalog_->StructuralReferences().size()),
      estimatedPersistentBytes_(catalog_->EstimatedBytes()),
      budgetExhausted_(estimatedPersistentBytes_ > limits_.maxPersistentBytes)
{
}

std::shared_ptr<MetadataIndexService> MetadataIndexService::Create(std::shared_ptr<const AbcDocument> document,
                                                                   std::shared_ptr<const MethodIndex> methods,
                                                                   std::shared_ptr<const MetadataCatalog> catalog,
                                                                   MetadataIndexLimits limits)
{
    if (document == nullptr || methods == nullptr || catalog == nullptr) {
        return nullptr;
    }
    auto decode = [document, methods](uint64_t methodOffset, const CancellationToken &cancellation) {
        ExecutableTarget target;
        target.methodOffset = methodOffset;
        return FunctionDisassembler {}.Disassemble(document->File(), *methods, target, {}, cancellation);
    };
    return std::make_shared<MetadataIndexService>(ConstructorToken {}, std::move(document), std::move(methods),
                                                  std::move(catalog), std::vector<MethodMetadata> {}, std::move(decode),
                                                  limits);
}

std::shared_ptr<MetadataIndexService> MetadataIndexService::CreateForTesting(
    std::shared_ptr<const MetadataCatalog> catalog, std::vector<MethodMetadata> methods,
    MetadataReferenceIndexer::DecodeMethod decode, MetadataIndexLimits limits)
{
    if (catalog == nullptr) {
        return nullptr;
    }
    std::sort(methods.begin(), methods.end(),
              [](const auto &left, const auto &right) { return left.methodOffset < right.methodOffset; });
    return std::make_shared<MetadataIndexService>(ConstructorToken {}, nullptr, nullptr, std::move(catalog),
                                                  std::move(methods), std::move(decode), limits);
}

MetadataIndexService::~MetadataIndexService()
{
    cancellation_.Cancel();
    runner_.WaitForIdle();
}

bool MetadataIndexService::Start()
{
    return StartWithPost([this] { runner_.Post([this] { RunWithFailureBoundary(); }); });
}

bool MetadataIndexService::StartWithPublishHookForTesting(std::function<void()> beforePublish)
{
    return StartWithPost([this, beforePublish = std::move(beforePublish)]() mutable {
        runner_.Post([this, beforePublish = std::move(beforePublish)]() mutable {
            RunWithPublishHookFailureBoundary(beforePublish);
        });
    });
}

void MetadataIndexService::RunWithFailureBoundary() noexcept
{
    try {
        Run();
    } catch (const std::exception &error) {
        MarkFailedFromException("Metadata reference index worker failed: ", error.what());
    } catch (...) {
        MarkFailedFromException("Metadata reference index worker failed: unknown failure");
    }
}

void MetadataIndexService::RunWithPublishHookFailureBoundary(std::function<void()> &beforePublish) noexcept
{
    try {
        RunWithPublish([this, &beforePublish](MetadataIndexBatch batch) {
            beforePublish();
            return Publish(std::move(batch));
        });
    } catch (const std::exception &error) {
        MarkFailedFromException("Metadata reference index worker failed: ", error.what());
    } catch (...) {
        MarkFailedFromException("Metadata reference index worker failed: unknown failure");
    }
}

void MetadataIndexService::MarkFailedFromException(std::string_view prefix, const char *detail) noexcept
{
    std::string diagnostic;
    try {
        diagnostic = MakeBoundedMetadataDiagnostic(prefix, detail);
    } catch (...) {
        // Best-effort: if building the bounded message throws, the failure is still recorded without a message.
    }
    MarkFailed(std::move(diagnostic));
}

void MetadataIndexService::MarkFailed(std::string diagnostic) noexcept
{
    std::shared_ptr<const std::vector<std::string>> previousDiagnostics;
    std::shared_ptr<const std::vector<std::string>> preparedDiagnostics;
    uint64_t preparedEstimatedBytes = 0U;
    bool preparedBudgetExhausted = false;
    bool prepared = false;
    try {
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            previousDiagnostics = diagnostics_;
            preparedEstimatedBytes = estimatedPersistentBytes_;
            preparedBudgetExhausted = budgetExhausted_;
        }
        preparedDiagnostics = previousDiagnostics;
        if (!diagnostic.empty()) {
            auto writableDiagnostics = std::make_shared<std::vector<std::string>>(*previousDiagnostics);
            StoreDiagnostic(std::move(diagnostic), limits_.maxPersistentBytes, &preparedEstimatedBytes,
                            &preparedBudgetExhausted, writableDiagnostics.get());
            preparedDiagnostics = std::move(writableDiagnostics);
        }
        prepared = true;
    } catch (...) {
        // Best-effort: keep the previously published diagnostics if preparing the updated snapshot throws.
    }
    try {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        phase_ = MetadataIndexPhase::FAILED;
        if (prepared && diagnostics_ == previousDiagnostics) {
            diagnostics_.swap(preparedDiagnostics);
            estimatedPersistentBytes_ = preparedEstimatedBytes;
            budgetExhausted_ = preparedBudgetExhausted;
        }
    } catch (...) {
        // Best-effort: leave the current phase and diagnostics untouched if publishing the FAILED state throws.
    }
}

void MetadataIndexService::Run()
{
    RunWithPublish([this](MetadataIndexBatch batch) { return Publish(std::move(batch)); });
}

void MetadataIndexService::RunWithPublish(MetadataReferenceIndexer::PublishBatch publish)
{
    bool budgetExhausted = false;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        budgetExhausted = budgetExhausted_;
    }
    if (budgetExhausted) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        phase_ = MetadataIndexPhase::PARTIAL;
        return;
    }
    auto result =
        MetadataReferenceIndexer::Build(*catalog_, Methods(), decode_, publish, limits_, cancellation_.Token());
    std::shared_ptr<const std::vector<std::string>> previousDiagnostics;
    size_t preparedScannedMethods = 0U;
    uint64_t preparedEstimatedBytes = 0U;
    bool preparedBudgetExhausted = false;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        previousDiagnostics = diagnostics_;
        preparedScannedMethods = std::max(scannedMethods_, result.scannedMethods);
        preparedEstimatedBytes = estimatedPersistentBytes_;
        preparedBudgetExhausted = budgetExhausted_;
    }
    auto preparedDiagnostics = PrepareRunDiagnostics(result, previousDiagnostics, limits_.maxPersistentBytes,
                                                     &preparedEstimatedBytes, &preparedBudgetExhausted);
    if (result.status == MetadataIndexBuildStatus::PARTIAL && preparedDiagnostics->empty() &&
        catalog_->Diagnostics().empty()) {
        preparedBudgetExhausted = true;
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    diagnostics_.swap(preparedDiagnostics);
    scannedMethods_ = preparedScannedMethods;
    estimatedPersistentBytes_ = preparedEstimatedBytes;
    budgetExhausted_ = preparedBudgetExhausted;
    phase_ = PhaseForBuildResult(result.status, catalog_->IsPartial());
}

// Stages the batch's keyed entries into the two staged maps, merging duplicates into the earlier
// entry.
void MetadataIndexService::StagePublishedBatch(
    MetadataIndexBatch *batch, std::map<MetadataObjectKey, MetadataObject, ObjectKeyLess> *stagedObjects,
    std::map<ReferenceSourceKey, MetadataReference, ReferenceSourceKeyLess> *stagedReferences)
{
    StageBatchEntries(
        batch->objects, stagedObjects, [](const MetadataObject &object) { return object.key; },
        [](MetadataObject *target, const MetadataObject &source) { MergeMetadataObjectEvidence(target, source); });
    StageBatchEntries(
        batch->references, stagedReferences,
        [](const MetadataReference &reference) { return MakeReferenceSourceKey(reference); },
        [](MetadataReference *target, const MetadataReference &source) { MergeReference(target, source); });
}

// Moves the staged entries into the published maps under the exclusive lock, accepting the batch
// only when the published revision still matches the one the preparation was built against.
template <typename StagedObjects, typename StagedReferences, typename Preparation>
bool MetadataIndexService::CommitPublish(StagedObjects *stagedObjects, StagedReferences *stagedReferences,
                                         Preparation *preparation)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (revision_ != preparation->expectedRevision) {
        phase_ = MetadataIndexPhase::FAILED;
        return false;
    }
    MergeStagedIntoDynamic(*stagedObjects, &dynamicObjects_);
    MergeStagedIntoDynamic(*stagedReferences, &dynamicReferences_);
    diagnostics_.swap(preparation->diagnostics);
    objectCount_ = preparation->objectCount;
    referenceCount_ = preparation->referenceCount;
    scannedMethods_ = preparation->scannedMethods;
    estimatedPersistentBytes_ = preparation->estimatedBytes;
    ++revision_;
    return true;
}

bool MetadataIndexService::Publish(MetadataIndexBatch batch)
{
    if (cancellation_.Token().IsCancellationRequested()) {
        return false;
    }

    std::map<MetadataObjectKey, MetadataObject, ObjectKeyLess> stagedObjects;
    std::map<ReferenceSourceKey, MetadataReference, ReferenceSourceKeyLess> stagedReferences;
    StagePublishedBatch(&batch, &stagedObjects, &stagedReferences);

    std::map<MetadataObjectKey, size_t, ObjectKeyLess> addedReferenceCounts;
    PublishPreparation preparation;
    std::vector<ObjectContext> objectContexts;
    std::vector<ReferenceContext> referenceContexts;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        preparation.objectCount = objectCount_;
        preparation.referenceCount = referenceCount_;
        preparation.scannedMethods = scannedMethods_ + batch.processedMethods;
        preparation.estimatedBytes = estimatedPersistentBytes_;
        preparation.expectedRevision = revision_;
        preparation.diagnostics = diagnostics_;
        CollectStagedContexts(stagedObjects, dynamicObjects_, &objectContexts);
        CollectStagedContexts(stagedReferences, dynamicReferences_, &referenceContexts);
    }

    ComputePublishVisibility(*catalog_, &objectContexts, &referenceContexts);
    ApplyStagedMerges(objectContexts, referenceContexts, &addedReferenceCounts, sizeof(ReferenceSourceKey),
                      &preparation);
    preparation.diagnostics =
        MergeBatchDiagnostics(std::move(batch.diagnostics), preparation.diagnostics, &preparation.estimatedBytes);
    if (preparation.estimatedBytes > limits_.maxPersistentBytes) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        budgetExhausted_ = true;
        return false;
    }
    if (cancellation_.Token().IsCancellationRequested()) {
        return false;
    }
    return CommitPublish(&stagedObjects, &stagedReferences, &preparation);
}

MetadataIndexStatus MetadataIndexService::Status() const
{
    MetadataIndexStatus status;
    std::shared_ptr<const std::vector<std::string>> diagnostics;
    bool budgetExhausted = false;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        status.phase = phase_;
        status.revision = revision_;
        status.scannedMethods = scannedMethods_;
        status.totalMethods = Methods().size();
        status.objectCount = objectCount_;
        status.referenceCount = referenceCount_;
        diagnostics = diagnostics_;
        budgetExhausted = budgetExhausted_;
    }
    const auto appendDiagnostic = [&status](const std::string &diagnostic) {
        if (std::find(status.diagnostics.begin(), status.diagnostics.end(), diagnostic) == status.diagnostics.end()) {
            if (status.diagnostics.size() < MAX_DIAGNOSTICS) {
                status.diagnostics.push_back(diagnostic);
            } else {
                status.diagnosticsTruncated = true;
            }
        }
    };
    for (const auto &diagnostic : catalog_->Diagnostics()) {
        appendDiagnostic(diagnostic);
    }
    for (const auto &diagnostic : *diagnostics) {
        appendDiagnostic(diagnostic);
    }
    if (budgetExhausted) {
        appendDiagnostic(std::string(BUDGET_DIAGNOSTIC));
    }
    return status;
}

MetadataPage<MetadataObject> MetadataIndexService::CollectObjectPage(const MetadataObjectQuery &query,
                                                                     size_t startIndex) const
{
    MetadataPage<MetadataObject> page;
    page.revision = revision_;
    page.items.reserve(query.limit);
    const auto &base = catalog_->Objects();
    size_t baseIndex = 0U;
    auto dynamic = dynamicObjects_.begin();
    while (baseIndex < base.size() || dynamic != dynamicObjects_.end()) {
        const MetadataObject *baseObject = nullptr;
        const MetadataObject *dynamicObject = nullptr;
        if (dynamic == dynamicObjects_.end() || (baseIndex < base.size() && base[baseIndex].key < dynamic->first)) {
            baseObject = &base[baseIndex++];
        } else if (baseIndex >= base.size() || dynamic->first < base[baseIndex].key) {
            dynamicObject = &dynamic++->second;
        } else {
            baseObject = &base[baseIndex++];
            dynamicObject = &dynamic++->second;
        }
        AppendObjectPageItem(baseObject, dynamicObject, query, startIndex, &page);
    }
    return page;
}

Result<MetadataPage<MetadataObject>> MetadataIndexService::QueryObjects(const MetadataObjectQuery &query) const
{
    if (query.limit == 0U || query.limit > MAX_PAGE_SIZE) {
        return Result<MetadataPage<MetadataObject>>::Failure(InvalidLimit());
    }
    if (query.query.size() > MAX_QUERY_BYTES) {
        return Result<MetadataPage<MetadataObject>>::Failure(
            panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata query exceeds 256 UTF-8 bytes"));
    }
    if (!IsValidUtf8(query.query)) {
        return Result<MetadataPage<MetadataObject>>::Failure(
            panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata query is not valid UTF-8"));
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto start = ParseCursor(query.cursor, revision_, ObjectFilterHash(query));
    if (!start.HasValue()) {
        return Result<MetadataPage<MetadataObject>>::Failure(start.Error());
    }
    auto page = CollectObjectPage(query, start.Value());
    if (start.Value() > page.total) {
        return Result<MetadataPage<MetadataObject>>::Failure(
            panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata cursor index is invalid"));
    }
    const auto nextIndex = start.Value() + page.items.size();
    if (nextIndex < page.total) {
        page.nextCursor = MakeCursor(revision_, nextIndex, ObjectFilterHash(query));
    }
    return Result<MetadataPage<MetadataObject>>::Success(std::move(page));
}

Result<MetadataObjectDetail> MetadataIndexService::QueryObject(const MetadataObjectKey &key) const
{
    MetadataObjectDetail detail;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto *base = catalog_->Find(key);
        const auto dynamic = dynamicObjects_.find(key);
        const bool hasDynamic = dynamic != dynamicObjects_.end();
        if (base == nullptr && !hasDynamic) {
            return Result<MetadataObjectDetail>::Failure(
                panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata object does not exist"));
        }
        detail.object = base != nullptr ? *base : dynamic->second;
        if (base != nullptr && hasDynamic) {
            MergeMetadataObjectEvidence(&detail.object, dynamic->second);
        }
    }
    if (key.kind == MetadataKind::STRING) {
        if (document_ == nullptr) {
            return Result<MetadataObjectDetail>::Failure(panda::bytecode_explorer::Status(
                ErrorCode::BACKEND_ERROR, "Metadata String detail requires an ABC document"));
        }
        auto value = ReadMetadataString(document_->File(), key.offset, MetadataLimits::MAX_DETAIL_BYTES);
        if (!value.HasValue()) {
            return Result<MetadataObjectDetail>::Failure(value.Error());
        }
        detail.value = std::move(value.Value().text);
        detail.valueTruncated = value.Value().truncated;
    }
    return Result<MetadataObjectDetail>::Success(std::move(detail));
}

MetadataPage<MetadataReference> MetadataIndexService::CollectReferencePage(const MetadataObjectKey &key, size_t limit,
                                                                           size_t startIndex) const
{
    MetadataPage<MetadataReference> page;
    page.revision = revision_;
    page.items.reserve(limit);
    const auto &base = catalog_->StructuralReferences();
    const auto baseBegin = std::lower_bound(
        base.begin(), base.end(), key,
        [](const MetadataReference &reference, const MetadataObjectKey &target) { return reference.target < target; });
    const auto baseEnd = std::upper_bound(
        baseBegin, base.end(), key,
        [](const MetadataObjectKey &target, const MetadataReference &reference) { return target < reference.target; });
    auto baseReference = baseBegin;
    auto dynamic = dynamicReferences_.lower_bound(key);
    const auto dynamicEnd = dynamicReferences_.upper_bound(key);
    while (baseReference != baseEnd || dynamic != dynamicEnd) {
        const MetadataReference *reference = nullptr;
        bool fromBase = false;
        if (dynamic == dynamicEnd ||
            (baseReference != baseEnd && ReferenceSourcePrefixLess(*baseReference, dynamic->second))) {
            reference = &*baseReference++;
            fromBase = true;
        } else {
            reference = &dynamic++->second;
        }
        if (fromBase && IsReferenceOverridden(*reference)) {
            continue;
        }
        if (page.total >= startIndex && page.items.size() < limit) {
            page.items.push_back(*reference);
        }
        ++page.total;
    }
    return page;
}

bool MetadataIndexService::IsReferenceOverridden(const MetadataReference &reference) const
{
    return dynamicReferences_.find(MakeReferenceSourceKey(reference)) != dynamicReferences_.end();
}

Result<MetadataPage<MetadataReference>> MetadataIndexService::QueryReferences(const MetadataObjectKey &key,
                                                                              std::optional<std::string> cursor,
                                                                              size_t limit) const
{
    if (limit == 0U || limit > MAX_PAGE_SIZE) {
        return Result<MetadataPage<MetadataReference>>::Failure(InvalidLimit());
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto dynamicObject = dynamicObjects_.find(key);
    if (catalog_->Find(key) == nullptr && dynamicObject == dynamicObjects_.end()) {
        return Result<MetadataPage<MetadataReference>>::Failure(
            panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata object does not exist"));
    }
    const auto start = ParseCursor(cursor, revision_, ReferenceFilterHash(key));
    if (!start.HasValue()) {
        return Result<MetadataPage<MetadataReference>>::Failure(start.Error());
    }
    auto page = CollectReferencePage(key, limit, start.Value());
    if (start.Value() > page.total) {
        return Result<MetadataPage<MetadataReference>>::Failure(
            panda::bytecode_explorer::Status(ErrorCode::INVALID_ARGUMENT, "Metadata cursor index is invalid"));
    }
    const auto nextIndex = start.Value() + page.items.size();
    if (nextIndex < page.total) {
        page.nextCursor = MakeCursor(revision_, nextIndex, ReferenceFilterHash(key));
    }
    return Result<MetadataPage<MetadataReference>>::Success(std::move(page));
}

void MetadataIndexService::WaitForIdleForTesting()
{
    runner_.WaitForIdle();
}

uint64_t MetadataIndexService::EstimatedPersistentBytesForTesting() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return estimatedPersistentBytes_;
}

const std::vector<MethodMetadata> &MetadataIndexService::Methods() const
{
    return methodIndex_ != nullptr ? methodIndex_->MethodsInOffsetOrder() : methods_;
}

}  // namespace panda::bytecode_explorer
