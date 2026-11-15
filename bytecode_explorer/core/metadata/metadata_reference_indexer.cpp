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

#include "core/metadata/metadata_reference_indexer.h"
#include "core/metadata/metadata_model.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

constexpr size_t MAX_BATCH_ENTRIES = 128U;
constexpr uint64_t MAX_BATCH_WORKING_BYTES = 256U * 1024U;

std::optional<MetadataKind> ToMetadataKind(IdOperandKind kind)
{
    switch (kind) {
        case IdOperandKind::STRING:
            return MetadataKind::STRING;
        case IdOperandKind::METHOD:
            return MetadataKind::METHOD;
        case IdOperandKind::LITERAL_ARRAY:
            return MetadataKind::LITERAL_ARRAY;
        case IdOperandKind::UNKNOWN:
            return std::nullopt;
    }
    return std::nullopt;
}

size_t ValidUtf8PrefixLength(std::string_view value, size_t maxBytes)
{
    size_t offset = 0U;
    while (offset < value.size() && offset < maxBytes) {
        const auto first = static_cast<uint8_t>(value[offset]);
        const auto length = metadata_model_detail::Utf8SequenceLength(first);
        if (length == 0U || length > value.size() - offset || length > maxBytes - offset) {
            break;
        }
        const auto *sequence = reinterpret_cast<const uint8_t *>(value.data()) + offset;
        if (!metadata_model_detail::IsValidUtf8Sequence(sequence, length)) {
            break;
        }
        offset += length;
    }
    return offset;
}

std::string BoundedText(std::string_view value, size_t maxBytes)
{
    return std::string(value.substr(0U, ValidUtf8PrefixLength(value, maxBytes)));
}

uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
    return right > std::numeric_limits<uint64_t>::max() - left ? std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t SaturatingMultiply(uint64_t left, uint64_t right)
{
    return left != 0U && right > std::numeric_limits<uint64_t>::max() / left ? std::numeric_limits<uint64_t>::max()
                                                                             : left * right;
}

uint64_t EstimateWorkingIndexEntry(const IdOperand &id, std::string_view mnemonic,
                                   const std::vector<EntityId> &sourceEntityIds)
{
    const auto previewBytes = std::min(id.resolvedText.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    const auto mnemonicBytes = std::min(mnemonic.size(), size_t {128U});
    uint64_t bytes = 3U * sizeof(MetadataObject) + 3U * sizeof(MetadataReference) + 256U;
    bytes = SaturatingAdd(bytes, 32U + previewBytes);
    bytes = SaturatingAdd(bytes, 32U + mnemonicBytes);
    bytes = SaturatingAdd(bytes, 2U * sizeof(MetadataOrigin));
    bytes =
        SaturatingAdd(bytes, SaturatingMultiply(2U * sizeof(EntityId), static_cast<uint64_t>(sourceEntityIds.size())));
    return bytes;
}

MetadataObject ObjectFromResolvedId(const IdOperand &id)
{
    MetadataObject object;
    object.key = {*ToMetadataKind(id.kind), *id.resolvedOffset};
    object.preview = BoundedText(id.resolvedText, MetadataLimits::MAX_PREVIEW_BYTES);
    object.truncated = object.preview.size() != id.resolvedText.size();
    object.origins.push_back(MetadataOrigin::INSTRUCTION);
    return object;
}

bool ReferenceLess(const MetadataReference &left, const MetadataReference &right)
{
    const auto leftRange = std::make_pair(left.sourceRange.has_value() ? left.sourceRange->offset : 0U,
                                          left.sourceRange.has_value() ? left.sourceRange->size : 0U);
    const auto rightRange = std::make_pair(right.sourceRange.has_value() ? right.sourceRange->offset : 0U,
                                           right.sourceRange.has_value() ? right.sourceRange->size : 0U);
    return std::tie(left.sourceMethodOffset, left.pc, left.operandIndex, left.target, left.kind, leftRange,
                    left.sourceEntityIds, left.sourceName, left.mnemonic) <
           std::tie(right.sourceMethodOffset, right.pc, right.operandIndex, right.target, right.kind, rightRange,
                    right.sourceEntityIds, right.sourceName, right.mnemonic);
}

bool ReferenceEqual(const MetadataReference &left, const MetadataReference &right)
{
    return !ReferenceLess(left, right) && !ReferenceLess(right, left);
}

bool SortAndUnique(MetadataIndexBatch *batch, const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    std::stable_sort(batch->objects.begin(), batch->objects.end(),
                     [](const auto &left, const auto &right) { return left.key < right.key; });
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    size_t retainedObjects = 0U;
    for (size_t index = 0U; index < batch->objects.size(); ++index) {
        if (retainedObjects == 0U || !(batch->objects[retainedObjects - 1U].key == batch->objects[index].key)) {
            if (retainedObjects != index) {
                batch->objects[retainedObjects] = std::move(batch->objects[index]);
            }
            ++retainedObjects;
        } else {
            MergeMetadataObjectEvidence(&batch->objects[retainedObjects - 1U], batch->objects[index]);
        }
    }
    batch->objects.resize(retainedObjects);
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    std::sort(batch->references.begin(), batch->references.end(), ReferenceLess);
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    batch->references.erase(std::unique(batch->references.begin(), batch->references.end(), ReferenceEqual),
                            batch->references.end());
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    std::sort(batch->diagnostics.begin(), batch->diagnostics.end());
    if (cancellation.IsCancellationRequested()) {
        return false;
    }
    batch->diagnostics.erase(std::unique(batch->diagnostics.begin(), batch->diagnostics.end()),
                             batch->diagnostics.end());
    return !cancellation.IsCancellationRequested();
}

std::string MethodDiagnosticPrefix(uint64_t methodOffset)
{
    std::array<char, 32U> offset {};
    const auto converted = std::to_chars(offset.data(), offset.data() + offset.size(), methodOffset);
    std::string prefix("Cannot index METHOD at offset ");
    prefix.append(offset.data(), converted.ptr);
    return prefix;
}

std::string MethodDiagnostic(uint64_t methodOffset, std::string_view diagnostic)
{
    auto prefix = MethodDiagnosticPrefix(methodOffset);
    if (diagnostic.empty()) {
        return MakeBoundedMetadataDiagnostic(prefix);
    }
    prefix += ": ";
    return MakeBoundedMetadataDiagnostic(prefix, diagnostic);
}

std::string MethodExceptionDiagnostic(uint64_t methodOffset, const char *diagnostic)
{
    auto prefix = MethodDiagnosticPrefix(methodOffset);
    if (diagnostic == nullptr || diagnostic[0U] == '\0') {
        return MakeBoundedMetadataDiagnostic(prefix);
    }
    prefix += ": ";
    return MakeBoundedMetadataDiagnostic(prefix, diagnostic);
}

uint64_t EstimateDiagnostic(const std::string &diagnostic)
{
    return 2U * sizeof(std::string) + 64U + 32U + diagnostic.capacity();
}

// Outcome of folding one resolved ID operand into the metadata batch.
enum class FoldOutcome {
    CONTINUE,             // operand handled; keep scanning
    CANCELLED,            // cancellation was requested; the per-method batch additions must be rolled back
    STOPPED_WITH_RESULT,  // result carries a terminal status; the caller must stop and return it
};

// Outcome of scanning one decoded method's instruction stream.
enum class ExtractionOutcome {
    COMPLETED,            // every instruction was folded into the batch
    CANCELLED,            // cancellation was requested mid-scan; the batch was rolled back
    STOPPED_WITH_RESULT,  // result carries a terminal status; the caller must stop and return it
};

// Everything the fold pipeline shares: the read-only inputs (catalog, cancellation), the
// working batch with its byte accounting, the build result under construction and the
// publish/diagnostic callbacks bound by Build.
struct FoldContext {
    const MetadataCatalog *catalog {nullptr};
    const CancellationToken *cancellation {nullptr};
    MetadataIndexBatch *batch {nullptr};
    uint64_t *batchDiagnosticBytes {nullptr};
    uint64_t *batchWorkingBytes {nullptr};
    bool *partial {nullptr};
    bool *publishFailed {nullptr};
    MetadataIndexBuildResult *result {nullptr};
    std::function<bool()> publishBatch;
    std::function<bool(std::string)> addBatchDiagnostic;
    std::function<void(std::string)> addResultDiagnostic;
};

// Per-method state of the fold pipeline: the method identity with its source entities and
// the rollback snapshot of the batch state captured before the method's operands were folded.
struct MethodFoldState {
    uint64_t methodOffset {0U};
    const std::vector<EntityId> *sourceEntityIds {nullptr};
    size_t objectCount {0U};
    size_t referenceCount {0U};
    uint64_t batchDiagnosticBytes {0U};
    uint64_t batchWorkingBytes {0U};
    bool workingLimitReported {false};
};

// Reports the bounded-working-batch violation once per method. On a persistent-budget
// exhaustion the batch is published and the terminal status is recorded in the result.
FoldOutcome ReportWorkingLimitOnce(uint64_t methodOffset, bool *workingLimitReported, const FoldContext &context)
{
    if (*workingLimitReported) {
        return FoldOutcome::CONTINUE;
    }
    auto diagnostic = MethodDiagnostic(methodOffset, "metadata reference exceeds the bounded working batch limit");
    if (!context.addBatchDiagnostic(diagnostic)) {
        const std::string budgetDiagnostic = "Metadata reference index persistent budget was exhausted";
        if (!context.publishBatch() && context.cancellation->IsCancellationRequested()) {
            context.result->status = MetadataIndexBuildStatus::CANCELLED;
            return FoldOutcome::STOPPED_WITH_RESULT;
        }
        if (*context.publishFailed) {
            return FoldOutcome::STOPPED_WITH_RESULT;
        }
        context.result->status = MetadataIndexBuildStatus::PARTIAL;
        context.addResultDiagnostic(budgetDiagnostic);
        return FoldOutcome::STOPPED_WITH_RESULT;
    }
    context.addResultDiagnostic(std::move(diagnostic));
    *workingLimitReported = true;
    return FoldOutcome::CONTINUE;
}

// Flushes the working batch before appending an entry that would cross the entry or byte
// budget, mirroring the original mid-method flush gate.
FoldOutcome FlushBatchBeforeEntry(MethodFoldState *methodState, const FoldContext &context, uint64_t entryBytes)
{
    const bool entryLimitReached = context.batch->references.size() >= MAX_BATCH_ENTRIES;
    const bool byteLimitReached = *context.batchWorkingBytes > MAX_BATCH_WORKING_BYTES ||
                                  entryBytes > MAX_BATCH_WORKING_BYTES - *context.batchWorkingBytes;
    if (!context.batch->references.empty() && (entryLimitReached || byteLimitReached)) {
        if (!context.publishBatch()) {
            if (*context.publishFailed) {
                return FoldOutcome::STOPPED_WITH_RESULT;
            }
            context.result->status = context.cancellation->IsCancellationRequested()
                                         ? MetadataIndexBuildStatus::CANCELLED
                                         : MetadataIndexBuildStatus::PARTIAL;
            if (context.result->status == MetadataIndexBuildStatus::PARTIAL) {
                context.addResultDiagnostic("Metadata reference index persistent budget was exhausted");
            }
            return FoldOutcome::STOPPED_WITH_RESULT;
        }
        methodState->objectCount = 0U;
        methodState->referenceCount = 0U;
        methodState->batchDiagnosticBytes = 0U;
        methodState->batchWorkingBytes = 0U;
    }
    return FoldOutcome::CONTINUE;
}

// Folds one instruction ID operand into the batch, mirroring the original operand-loop body:
// reference construction, working-limit reporting and mid-method batch flushes.
FoldOutcome FoldIdOperand(const IdOperand &id, const InstructionInfo &instruction, size_t operand,
                          MethodFoldState *methodState, const FoldContext &context)
{
    if (context.cancellation->IsCancellationRequested()) {
        return FoldOutcome::CANCELLED;
    }
    if (!id.resolvedOffset.has_value() || id.kind == IdOperandKind::UNKNOWN) {
        return FoldOutcome::CONTINUE;
    }
    const auto kind = ToMetadataKind(id.kind);
    if (!kind.has_value()) {
        return FoldOutcome::CONTINUE;
    }
    const auto entryBytes = EstimateWorkingIndexEntry(id, instruction.mnemonic, *methodState->sourceEntityIds);
    if (entryBytes > MAX_BATCH_WORKING_BYTES) {
        *context.partial = true;
        return ReportWorkingLimitOnce(methodState->methodOffset, &methodState->workingLimitReported, context);
    }
    const auto flushed = FlushBatchBeforeEntry(methodState, context, entryBytes);
    if (flushed != FoldOutcome::CONTINUE) {
        return flushed;
    }
    *context.batchWorkingBytes = SaturatingAdd(*context.batchWorkingBytes, entryBytes);
    MetadataReference reference;
    reference.target = {*kind, *id.resolvedOffset};
    reference.kind = MetadataReferenceKind::INSTRUCTION_OPERAND;
    reference.sourceEntityIds = *methodState->sourceEntityIds;
    reference.sourceMethodOffset = methodState->methodOffset;
    reference.pc = instruction.pc;
    reference.operandIndex = static_cast<uint32_t>(operand);
    if (instruction.fileRange.size != 0U) {
        reference.sourceRange = instruction.fileRange;
    }
    reference.mnemonic = BoundedText(instruction.mnemonic, 128U);
    context.batch->objects.push_back(ObjectFromResolvedId(id));
    context.batch->references.push_back(std::move(reference));
    return FoldOutcome::CONTINUE;
}

// Scans one decoded method's instructions and folds every resolved ID operand into the batch.
// On cancellation the per-method batch additions are rolled back before returning.
ExtractionOutcome ExtractMethodReferences(const DisassemblyResult &decoded, uint64_t methodOffset,
                                          const FoldContext &context)
{
    MethodFoldState methodState;
    methodState.methodOffset = methodOffset;
    methodState.sourceEntityIds = &context.catalog->EntitiesForMethod(methodOffset);
    methodState.objectCount = context.batch->objects.size();
    methodState.referenceCount = context.batch->references.size();
    methodState.batchDiagnosticBytes = *context.batchDiagnosticBytes;
    methodState.batchWorkingBytes = *context.batchWorkingBytes;
    const auto rollback = [methodState, &context]() {
        context.batch->objects.resize(methodState.objectCount);
        context.batch->references.resize(methodState.referenceCount);
        *context.batchDiagnosticBytes = methodState.batchDiagnosticBytes;
        *context.batchWorkingBytes = methodState.batchWorkingBytes;
    };
    for (const auto &instruction : decoded.disassembly->instructions) {
        if (context.cancellation->IsCancellationRequested()) {
            rollback();
            return ExtractionOutcome::CANCELLED;
        }
        for (size_t operand = 0U; operand < instruction.ids.size(); ++operand) {
            const auto outcome = FoldIdOperand(instruction.ids[operand], instruction, operand, &methodState, context);
            if (outcome == FoldOutcome::CANCELLED) {
                rollback();
                return ExtractionOutcome::CANCELLED;
            }
            if (outcome == FoldOutcome::STOPPED_WITH_RESULT) {
                return ExtractionOutcome::STOPPED_WITH_RESULT;
            }
        }
    }
    return ExtractionOutcome::COMPLETED;
}

// Records the failure diagnostic of a method whose decode did not succeed. Returns true when
// the persistent budget is exhausted and the result carries the terminal status.
bool RecordFailedDecodedMethod(uint64_t methodOffset, std::string decodeDiagnostic, const FoldContext &context)
{
    *context.partial = true;
    auto diagnostic = MethodDiagnostic(methodOffset, decodeDiagnostic);
    if (!context.addBatchDiagnostic(diagnostic)) {
        const std::string budgetDiagnostic = "Metadata reference index persistent budget was exhausted";
        (void)context.addBatchDiagnostic(budgetDiagnostic);
        if (!context.publishBatch() && context.cancellation->IsCancellationRequested()) {
            context.result->status = MetadataIndexBuildStatus::CANCELLED;
            return true;
        }
        if (*context.publishFailed) {
            return true;
        }
        context.result->status = MetadataIndexBuildStatus::PARTIAL;
        context.addResultDiagnostic(budgetDiagnostic);
        return true;
    }
    context.addResultDiagnostic(std::move(diagnostic));
    return false;
}

// Budget ledger for one Build run: tracks the bytes already accounted as persistent so
// diagnostics can be rejected once the budget is exhausted.
struct BuildLedger {
    const MetadataIndexLimits *limits {nullptr};
    uint64_t accountedPersistentBytes {0U};
    uint64_t batchDiagnosticBytes {0U};
    uint64_t resultDiagnosticBytes {0U};

    bool CanRetain(uint64_t bytes) const
    {
        auto total = SaturatingAdd(accountedPersistentBytes, batchDiagnosticBytes);
        total = SaturatingAdd(total, resultDiagnosticBytes);
        return total <= limits->maxPersistentBytes && bytes <= limits->maxPersistentBytes - total;
    }
};

// Retains a result diagnostic when the persistent budget still allows it.
void AddResultDiagnostic(BuildLedger *ledger, MetadataIndexBuildResult *result, std::string diagnostic)
{
    const auto bytes = EstimateDiagnostic(diagnostic);
    if (!ledger->CanRetain(bytes)) {
        return;
    }
    ledger->resultDiagnosticBytes += bytes;
    result->diagnostics.push_back(std::move(diagnostic));
}

// Retains a batch diagnostic when the persistent budget still allows it.
bool AddBatchDiagnostic(BuildLedger *ledger, MetadataIndexBatch *batch, std::string diagnostic)
{
    const auto bytes = EstimateDiagnostic(diagnostic);
    if (!ledger->CanRetain(bytes)) {
        return false;
    }
    ledger->batchDiagnosticBytes += bytes;
    batch->diagnostics.push_back(std::move(diagnostic));
    return true;
}

// Everything one Build run needs beyond the fold pipeline: the budget ledger, the working
// batch with its publish timing, the terminal flags and the result under construction.
struct BuildContext {
    const CancellationToken *cancellation {nullptr};
    const MetadataReferenceIndexer::PublishBatch *publish {nullptr};
    BuildLedger ledger;
    MetadataIndexBatch batch;
    uint64_t batchWorkingBytes {0U};
    std::chrono::steady_clock::time_point lastPublish {};
    bool partial {false};
    bool publishFailed {false};
    MetadataIndexBuildResult result;
};

// Publishes the current batch when non-empty, resetting the batch state on success.
bool PublishCurrentBatch(BuildContext *context)
{
    auto &batch = context->batch;
    if (batch.processedMethods == 0U && batch.objects.empty() && batch.references.empty() &&
        batch.diagnostics.empty()) {
        return true;
    }
    if (!SortAndUnique(&batch, *context->cancellation) || context->cancellation->IsCancellationRequested()) {
        return false;
    }
    const auto retainedBytes = context->ledger.batchDiagnosticBytes;
    try {
        if (!(*context->publish)(std::move(batch))) {
            return false;
        }
    } catch (const std::exception &error) {
        context->publishFailed = true;
        context->result.status = MetadataIndexBuildStatus::FAILED;
        AddResultDiagnostic(&context->ledger, &context->result,
                            MakeBoundedMetadataDiagnostic("Cannot publish metadata reference index: ", error.what()));
        return false;
    } catch (...) {
        context->publishFailed = true;
        context->result.status = MetadataIndexBuildStatus::FAILED;
        AddResultDiagnostic(&context->ledger, &context->result,
                            "Cannot publish metadata reference index: unknown failure");
        return false;
    }
    context->ledger.accountedPersistentBytes = SaturatingAdd(context->ledger.accountedPersistentBytes, retainedBytes);
    batch = {};
    context->ledger.batchDiagnosticBytes = 0U;
    context->batchWorkingBytes = 0U;
    context->lastPublish = std::chrono::steady_clock::now();
    return !context->cancellation->IsCancellationRequested();
}

// Applies the terminal status of a failed publish to the result: keeps FAILED when the
// publish callback threw, otherwise reports CANCELLED or a budget-exhausted PARTIAL.
void MarkPublishFailure(BuildContext *context, const FoldContext &foldContext)
{
    if (context->publishFailed) {
        return;
    }
    if (context->cancellation->IsCancellationRequested()) {
        context->result.status = MetadataIndexBuildStatus::CANCELLED;
        return;
    }
    context->result.status = MetadataIndexBuildStatus::PARTIAL;
    foldContext.addResultDiagnostic("Metadata reference index persistent budget was exhausted");
}

// Scans one method's references into the batch. Returns false when the result is terminal
// (failed, cancelled or stopped by a budget report) and the caller must return it.
bool FoldMethodIntoBatch(const MethodMetadata &method, const MetadataReferenceIndexer::DecodeMethod &decode,
                         const FoldContext &foldContext, BuildContext *context)
{
    auto &result = context->result;
    ++result.scannedMethods;
    ++context->batch.processedMethods;
    if (!method.codeOffset.has_value()) {
        return true;
    }
    DisassemblyResult decoded;
    try {
        decoded = decode(method.methodOffset, *context->cancellation);
    } catch (const std::exception &error) {
        result.status = MetadataIndexBuildStatus::FAILED;
        foldContext.addResultDiagnostic(MethodExceptionDiagnostic(method.methodOffset, error.what()));
        return false;
    } catch (...) {
        result.status = MetadataIndexBuildStatus::FAILED;
        foldContext.addResultDiagnostic(MethodDiagnostic(method.methodOffset, "unknown decode failure"));
        return false;
    }
    if (decoded.status == DisassemblyStatus::SUCCESS && decoded.disassembly != nullptr) {
        const auto extraction = ExtractMethodReferences(decoded, method.methodOffset, foldContext);
        if (extraction == ExtractionOutcome::CANCELLED) {
            decoded.disassembly.reset();
            result.status = MetadataIndexBuildStatus::CANCELLED;
            return false;
        }
        if (extraction == ExtractionOutcome::STOPPED_WITH_RESULT) {
            return false;
        }
    } else if (decoded.status == DisassemblyStatus::CANCELLED || context->cancellation->IsCancellationRequested()) {
        decoded.disassembly.reset();
        result.status = MetadataIndexBuildStatus::CANCELLED;
        return false;
    } else {
        if (RecordFailedDecodedMethod(method.methodOffset, decoded.diagnostic, foldContext)) {
            return false;
        }
    }
    decoded.disassembly.reset();
    return true;
}

// Folds every method into batches, publishing each batch once its method count or time interval
// limit is reached.
bool FoldMethodsIntoBatches(const std::vector<MethodMetadata> &methods,
                            const MetadataReferenceIndexer::DecodeMethod &decode, const FoldContext &foldContext,
                            const MetadataIndexLimits &limits, BuildContext *context)
{
    for (const auto &method : methods) {
        if (foldContext.cancellation->IsCancellationRequested()) {
            (void)PublishCurrentBatch(context);
            context->result.status = MetadataIndexBuildStatus::CANCELLED;
            return false;
        }
        if (!FoldMethodIntoBatch(method, decode, foldContext, context)) {
            return false;
        }
        if (foldContext.cancellation->IsCancellationRequested()) {
            (void)PublishCurrentBatch(context);
            context->result.status = MetadataIndexBuildStatus::CANCELLED;
            return false;
        }
        const bool intervalElapsed = limits.batchInterval <= std::chrono::milliseconds::zero() ||
                                     std::chrono::steady_clock::now() - context->lastPublish >= limits.batchInterval;
        if ((context->batch.processedMethods >= limits.batchMethods || intervalElapsed) &&
            !PublishCurrentBatch(context)) {
            MarkPublishFailure(context, foldContext);
            return false;
        }
    }
    return true;
}

// Binds the publish/diagnostic callbacks of a build context together with the catalog and
// cancellation handles into the fold pipeline context used by the method folding helpers.
FoldContext MakeFoldContext(const MetadataCatalog &catalog, const CancellationToken &cancellation,
                            BuildContext *context)
{
    const auto publishBatch = [context]() { return PublishCurrentBatch(context); };
    const auto addBatchDiagnostic = [context](std::string diagnostic) {
        return AddBatchDiagnostic(&context->ledger, &context->batch, std::move(diagnostic));
    };
    const auto addResultDiagnostic = [context](std::string diagnostic) {
        AddResultDiagnostic(&context->ledger, &context->result, std::move(diagnostic));
    };
    const FoldContext foldContext {&catalog,
                                   &cancellation,
                                   &context->batch,
                                   &context->ledger.batchDiagnosticBytes,
                                   &context->batchWorkingBytes,
                                   &context->partial,
                                   &context->publishFailed,
                                   &context->result,
                                   publishBatch,
                                   addBatchDiagnostic,
                                   addResultDiagnostic};
    return foldContext;
}

}  // namespace

MetadataIndexBuildResult MetadataReferenceIndexer::Build(const MetadataCatalog &catalog,
                                                         const std::vector<MethodMetadata> &methods,
                                                         const DecodeMethod &decode, const PublishBatch &publish,
                                                         const MetadataIndexLimits &limits,
                                                         const CancellationToken &cancellation)
{
    MetadataIndexBuildResult result;
    if (catalog.EstimatedBytes() > limits.maxPersistentBytes) {
        result.status = MetadataIndexBuildStatus::PARTIAL;
        result.diagnostics.emplace_back("Metadata reference index persistent budget was exhausted");
        return result;
    }
    BuildContext context;
    context.cancellation = &cancellation;
    context.publish = &publish;
    context.ledger.limits = &limits;
    context.ledger.accountedPersistentBytes = catalog.EstimatedBytes();
    context.lastPublish = std::chrono::steady_clock::now();
    if ((!decode && !methods.empty()) || !publish || limits.batchMethods == 0U) {
        context.result.status = MetadataIndexBuildStatus::FAILED;
        AddResultDiagnostic(&context.ledger, &context.result, "Metadata reference index configuration is invalid");
        return context.result;
    }

    const FoldContext foldContext = MakeFoldContext(catalog, cancellation, &context);
    if (!FoldMethodsIntoBatches(methods, decode, foldContext, limits, &context)) {
        return context.result;
    }

    if (!PublishCurrentBatch(&context)) {
        MarkPublishFailure(&context, foldContext);
        return context.result;
    }
    context.result.status = context.partial ? MetadataIndexBuildStatus::PARTIAL : MetadataIndexBuildStatus::READY;
    return context.result;
}

}  // namespace panda::bytecode_explorer
