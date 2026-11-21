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

#include "core/metadata/metadata_catalog.h"

#include "core/index/structure_index.h"
#include "core/model/physical_program.h"

#include "libpandafile/file-inl.h"
#include "libpandafile/method_data_accessor-inl.h"
#include "utils/utf.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

Status InvalidMetadata(std::string message)
{
    return Status(ErrorCode::INVALID_FORMAT, "Invalid metadata evidence: " + std::move(message));
}

template <class T>
void SortUnique(std::vector<T> *values)
{
    std::sort(values->begin(), values->end());
    values->erase(std::unique(values->begin(), values->end()), values->end());
}

bool AppendUtf8(uint32_t codePoint, size_t maxTextBytes, std::string *output)
{
    uint8_t bytes[4U] {};
    uint8_t *next = bytes;
    size_t count = 0U;
    if (codePoint <= 0x7FU) {
        *next++ = static_cast<uint8_t>(codePoint);
        count = 1U;
    } else if (codePoint <= 0x7FFU) {
        *next++ = static_cast<uint8_t>(0xC0U | (codePoint >> 6U));
        *next++ = static_cast<uint8_t>(0x80U | (codePoint & 0x3FU));
        count = 2U;
    } else if (codePoint <= 0xFFFFU) {
        *next++ = static_cast<uint8_t>(0xE0U | (codePoint >> 12U));
        *next++ = static_cast<uint8_t>(0x80U | ((codePoint >> 6U) & 0x3FU));
        *next++ = static_cast<uint8_t>(0x80U | (codePoint & 0x3FU));
        count = 3U;
    } else {
        *next++ = static_cast<uint8_t>(0xF0U | (codePoint >> 18U));
        *next++ = static_cast<uint8_t>(0x80U | ((codePoint >> 12U) & 0x3FU));
        *next++ = static_cast<uint8_t>(0x80U | ((codePoint >> 6U) & 0x3FU));
        *next++ = static_cast<uint8_t>(0x80U | (codePoint & 0x3FU));
        count = 4U;
    }
    if (output->size() > maxTextBytes || count > maxTextBytes - output->size()) {
        return false;
    }
    output->append(reinterpret_cast<const char *>(bytes), count);
    return true;
}

// Leading-byte forms of the modified UTF-8 (MUTF-8) used by panda string data.
struct Mutf8LeadingByte {
    uint8_t mask;
    uint8_t value;
    size_t length;
    uint32_t payloadMask;
    uint32_t minCodePoint;
};

constexpr Mutf8LeadingByte MUTF8_LEADING_BYTES[] = {
    {0xE0U, 0xC0U, 2U, 0x1FU, 0x80U}, {0xF0U, 0xE0U, 3U, 0x0FU, 0x800U}, {0xF8U, 0xF0U, 4U, 0x07U, 0x10000U}};

constexpr uint32_t MAX_UNICODE_CODE_POINT = 0x10FFFFU;
constexpr uint8_t UTF8_CONTINUATION_MASK = 0xC0U;
constexpr uint8_t UTF8_CONTINUATION_VALUE = 0x80U;
constexpr uint8_t UTF8_PAYLOAD_MASK = 0x3FU;

Result<std::pair<uint32_t, size_t>> DecodeMutf8CodeUnit(const uint8_t *data, size_t remaining)
{
    const auto invalid = []() {
        return Result<std::pair<uint32_t, size_t>>::Failure(InvalidMetadata("malformed MUTF-8 string"));
    };
    if (remaining == 0U || data[0] == 0U) {
        return invalid();
    }
    if ((data[0] & 0x80U) == 0U) {
        return Result<std::pair<uint32_t, size_t>>::Success({data[0], 1U});
    }
    for (const Mutf8LeadingByte &leading : MUTF8_LEADING_BYTES) {
        if ((data[0] & leading.mask) != leading.value) {
            continue;
        }
        if (remaining < leading.length) {
            return invalid();
        }
        uint32_t codePoint = data[0] & leading.payloadMask;
        for (size_t index = 1U; index < leading.length; ++index) {
            if ((data[index] & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_VALUE) {
                return invalid();
            }
            codePoint = (codePoint << 6U) | (data[index] & UTF8_PAYLOAD_MASK);
        }
        // Modified UTF-8 keeps the overlong two-byte form 0xC0 0x80 as the encoding of U+0000;
        // any other sequence below the minimum of its length stays malformed.
        if ((codePoint < leading.minCodePoint && !(codePoint == 0U && leading.length == 2U)) ||
            codePoint > MAX_UNICODE_CODE_POINT) {
            return invalid();
        }
        return Result<std::pair<uint32_t, size_t>>::Success({codePoint, leading.length});
    }
    return invalid();
}

MetadataKind PublicKind(PhysicalMetadataTargetKind kind)
{
    switch (kind) {
        case PhysicalMetadataTargetKind::STRING:
            return MetadataKind::STRING;
        case PhysicalMetadataTargetKind::METHOD:
            return MetadataKind::METHOD;
        case PhysicalMetadataTargetKind::LITERAL_ARRAY:
            return MetadataKind::LITERAL_ARRAY;
    }
    return MetadataKind::STRING;
}

MetadataReferenceKind PublicReferenceKind(PhysicalMetadataReferenceKind kind)
{
    switch (kind) {
        case PhysicalMetadataReferenceKind::RECORD_NAME:
            return MetadataReferenceKind::RECORD_NAME;
        case PhysicalMetadataReferenceKind::METHOD_NAME:
            return MetadataReferenceKind::METHOD_NAME;
        case PhysicalMetadataReferenceKind::FIELD_NAME:
            return MetadataReferenceKind::FIELD_NAME;
        case PhysicalMetadataReferenceKind::ANNOTATION_NAME:
            return MetadataReferenceKind::ANNOTATION_NAME;
        case PhysicalMetadataReferenceKind::ANNOTATION_ELEMENT_NAME:
            return MetadataReferenceKind::ANNOTATION_ELEMENT_NAME;
        case PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE:
            return MetadataReferenceKind::ANNOTATION_STRING_VALUE;
        case PhysicalMetadataReferenceKind::MODULE_REQUEST:
            return MetadataReferenceKind::MODULE_REQUEST;
        case PhysicalMetadataReferenceKind::IMPORT_NAME:
            return MetadataReferenceKind::IMPORT_NAME;
        case PhysicalMetadataReferenceKind::EXPORT_NAME:
            return MetadataReferenceKind::EXPORT_NAME;
        case PhysicalMetadataReferenceKind::LOCAL_NAME:
            return MetadataReferenceKind::LOCAL_NAME;
        case PhysicalMetadataReferenceKind::SCOPE_NAME:
            return MetadataReferenceKind::SCOPE_NAME;
        case PhysicalMetadataReferenceKind::LITERAL_STRING:
            return MetadataReferenceKind::LITERAL_STRING;
        case PhysicalMetadataReferenceKind::LITERAL_METHOD:
            return MetadataReferenceKind::LITERAL_METHOD;
        case PhysicalMetadataReferenceKind::NESTED_LITERAL_ARRAY:
            return MetadataReferenceKind::NESTED_LITERAL_ARRAY;
        case PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE:
            return MetadataReferenceKind::INSTRUCTION_OPERAND;
    }
    return MetadataReferenceKind::RECORD_NAME;
}

MetadataOrigin OriginFor(PhysicalMetadataReferenceKind kind)
{
    switch (kind) {
        case PhysicalMetadataReferenceKind::LITERAL_STRING:
        case PhysicalMetadataReferenceKind::LITERAL_METHOD:
        case PhysicalMetadataReferenceKind::NESTED_LITERAL_ARRAY:
            return MetadataOrigin::LITERAL;
        case PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE:
            return MetadataOrigin::INSTRUCTION;
        default:
            return MetadataOrigin::STRUCTURE;
    }
}

bool IsRequiredStringEvidence(PhysicalMetadataReferenceKind kind)
{
    return kind == PhysicalMetadataReferenceKind::RECORD_NAME || kind == PhysicalMetadataReferenceKind::METHOD_NAME ||
           kind == PhysicalMetadataReferenceKind::FIELD_NAME;
}

PhysicalEntityKind SourceEntityKind(PhysicalMetadataReferenceKind kind)
{
    switch (kind) {
        case PhysicalMetadataReferenceKind::RECORD_NAME:
            return PhysicalEntityKind::RECORD;
        case PhysicalMetadataReferenceKind::METHOD_NAME:
        case PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE:
            return PhysicalEntityKind::METHOD;
        case PhysicalMetadataReferenceKind::FIELD_NAME:
            return PhysicalEntityKind::FIELD;
        case PhysicalMetadataReferenceKind::ANNOTATION_NAME:
        case PhysicalMetadataReferenceKind::ANNOTATION_ELEMENT_NAME:
        case PhysicalMetadataReferenceKind::ANNOTATION_STRING_VALUE:
            return PhysicalEntityKind::ANNOTATION;
        case PhysicalMetadataReferenceKind::MODULE_REQUEST:
        case PhysicalMetadataReferenceKind::IMPORT_NAME:
        case PhysicalMetadataReferenceKind::EXPORT_NAME:
        case PhysicalMetadataReferenceKind::LOCAL_NAME:
            return PhysicalEntityKind::MODULE_DATA;
        case PhysicalMetadataReferenceKind::SCOPE_NAME:
        case PhysicalMetadataReferenceKind::LITERAL_STRING:
        case PhysicalMetadataReferenceKind::LITERAL_METHOD:
        case PhysicalMetadataReferenceKind::NESTED_LITERAL_ARRAY:
            return PhysicalEntityKind::LITERAL_ARRAY;
    }
    return PhysicalEntityKind::FILE;
}

struct PhysicalBackingKey {
    PhysicalEntityKind kind {PhysicalEntityKind::FILE};
    uint64_t offset {0U};

    bool operator<(const PhysicalBackingKey &other) const
    {
        return std::tie(kind, offset) < std::tie(other.kind, other.offset);
    }
};

uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
    return right > std::numeric_limits<uint64_t>::max() - left ? std::numeric_limits<uint64_t>::max() : left + right;
}

uint64_t EstimateString(const std::string &value)
{
    constexpr uint64_t allocationOverhead = 32U;
    return SaturatingAdd(allocationOverhead, value.capacity());
}

uint64_t EstimateObject(const MetadataObject &object)
{
    // Charge a second element for conservative outer-vector growth.
    uint64_t result = 2U * sizeof(MetadataObject) + 64U;
    result = SaturatingAdd(result, EstimateString(object.preview));
    result = SaturatingAdd(result, object.origins.capacity() * sizeof(MetadataOrigin));
    result = SaturatingAdd(result, object.entityIds.capacity() * sizeof(EntityId));
    result = SaturatingAdd(result, object.diagnostics.capacity() * sizeof(std::string));
    for (const auto &diagnostic : object.diagnostics) {
        result = SaturatingAdd(result, EstimateString(diagnostic));
    }
    return result;
}

uint64_t EstimateReference(const MetadataReference &reference)
{
    // Charge a second element for conservative outer-vector growth.
    uint64_t result = 2U * sizeof(MetadataReference) + 64U;
    result = SaturatingAdd(result, reference.sourceEntityIds.capacity() * sizeof(EntityId));
    result = SaturatingAdd(result, EstimateString(reference.sourceName));
    result = SaturatingAdd(result, EstimateString(reference.mnemonic));
    return result;
}

uint64_t EstimateDiagnostic(const std::string &diagnostic)
{
    return SaturatingAdd(2U * sizeof(std::string) + 64U, EstimateString(diagnostic));
}

std::string BoundedText(const std::string &value, size_t maxBytes = MetadataLimits::MAX_PREVIEW_BYTES)
{
    std::string bounded;
    bounded.reserve(std::min(value.size(), maxBytes));
    metadata_model_detail::AppendBoundedUtf8(&bounded, std::string_view(value), maxBytes);
    return bounded;
}

namespace catalog_build_detail {

class BuildBudget final {
public:
    explicit BuildBudget(uint64_t limit) : limit_(limit) {}

    bool TryPersistent(uint64_t bytes)
    {
        return Try(bytes, 0U);
    }

    bool TryWorking(uint64_t bytes)
    {
        return Try(0U, bytes);
    }

    bool Try(uint64_t persistentBytes, uint64_t workingBytes)
    {
        const auto addition = SaturatingAdd(persistentBytes, workingBytes);
        const auto current = SaturatingAdd(persistentBytes_, workingBytes_);
        if (current > limit_ || addition > limit_ - current) {
            return false;
        }
        persistentBytes_ += persistentBytes;
        workingBytes_ += workingBytes;
        peakBytes_ = std::max(peakBytes_, persistentBytes_ + workingBytes_);
        return true;
    }

    void ReleaseWorking(uint64_t bytes)
    {
        workingBytes_ -= std::min(workingBytes_, bytes);
    }

    uint64_t PersistentBytes() const
    {
        return persistentBytes_;
    }

    uint64_t PeakBytes() const
    {
        return peakBytes_;
    }

private:
    uint64_t limit_ {0U};
    uint64_t persistentBytes_ {0U};
    uint64_t workingBytes_ {0U};
    uint64_t peakBytes_ {0U};
};

struct BackingInfo {
    std::vector<EntityId> entityIds;
    std::string preview;
};

}  // namespace catalog_build_detail

using catalog_build_detail::BackingInfo;
using catalog_build_detail::BuildBudget;

constexpr uint64_t MAP_NODE_OVERHEAD = 64U;
constexpr uint64_t OBJECT_INDEX_WORKING_BYTES = sizeof(MetadataObjectKey) + sizeof(size_t) + MAP_NODE_OVERHEAD;
constexpr uint64_t INVALID_STRING_WORKING_BYTES = sizeof(uint64_t) + MAP_NODE_OVERHEAD;
constexpr uint64_t STRING_READ_WORKING_BYTES =
    2U * sizeof(MetadataStringRead) + MetadataLimits::MAX_PREVIEW_BYTES + MAP_NODE_OVERHEAD;

uint64_t EstimateBackingInfo(const BackingInfo &info)
{
    uint64_t result = sizeof(PhysicalBackingKey) + sizeof(BackingInfo) + MAP_NODE_OVERHEAD;
    result = SaturatingAdd(result, 2U * info.entityIds.capacity() * sizeof(EntityId));
    return SaturatingAdd(result, EstimateString(info.preview));
}

size_t NextVectorCapacity(size_t capacity, size_t minimum)
{
    if (capacity >= minimum) {
        return capacity;
    }
    const auto doubled = capacity > std::numeric_limits<size_t>::max() / 2U ? std::numeric_limits<size_t>::max()
                                                                            : std::max<size_t>(1U, capacity * 2U);
    return std::max(doubled, minimum);
}

std::string OffsetDiagnostic(const char *what, uint64_t offset)
{
    std::ostringstream message;
    message << what << " at offset " << offset;
    return message.str();
}

// One decoded MUTF-8 code point: the scalar value, the payload bytes it consumed and the
// number of UTF-16 units it occupies after surrogate pairing.
struct Mutf8CodePoint {
    uint32_t codePoint {0U};
    size_t consumed {1U};
    uint32_t utf16Units {1U};
};

// Decodes one MUTF-8 code point, pairing a high surrogate with the low surrogate that
// follows it and rejecting unpaired surrogates outright.
Result<Mutf8CodePoint> DecodeMutf8CodePoint(const uint8_t *payload, size_t remaining)
{
    auto decoded = DecodeMutf8CodeUnit(payload, remaining);
    if (!decoded.HasValue()) {
        return Result<Mutf8CodePoint>::Failure(decoded.Error());
    }
    Mutf8CodePoint point {decoded.Value().first, decoded.Value().second, 1U};
    if (point.codePoint >= utf::HI_SURROGATE_MIN && point.codePoint <= utf::HI_SURROGATE_MAX) {
        auto low = DecodeMutf8CodeUnit(payload + point.consumed, remaining - point.consumed);
        if (!low.HasValue() || low.Value().first < utf::LO_SURROGATE_MIN || low.Value().first > utf::LO_SURROGATE_MAX) {
            return Result<Mutf8CodePoint>::Failure(InvalidMetadata("unpaired MUTF-8 high surrogate"));
        }
        point.codePoint =
            ((point.codePoint - utf::HI_SURROGATE_MIN) << 10U) + (low.Value().first - utf::LO_SURROGATE_MIN) + 0x10000U;
        point.consumed += low.Value().second;
        point.utf16Units = 2U;
        return Result<Mutf8CodePoint>::Success(point);
    }
    if (point.codePoint >= utf::LO_SURROGATE_MIN && point.codePoint <= utf::LO_SURROGATE_MAX) {
        return Result<Mutf8CodePoint>::Failure(InvalidMetadata("unpaired MUTF-8 low surrogate"));
    }
    if (point.codePoint > 0xFFFFU) {
        point.utf16Units = 2U;
    }
    return Result<Mutf8CodePoint>::Success(point);
}

// Decodes the ULEB128-tagged UTF-16 length prefix of a metadata string and returns the
// payload start offset together with the expected UTF-16 length.
Result<std::pair<uint64_t, uint32_t>> ReadMutf8LengthPrefix(const panda_file::File &file, uint64_t offset)
{
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (offset >= fileSize) {
        return Result<std::pair<uint64_t, uint32_t>>::Failure(
            InvalidMetadata(OffsetDiagnostic("string is outside the file", offset)));
    }
    const auto *base = file.GetBase();
    uint64_t textOffset = offset;
    uint32_t taggedUtf16Length = 0U;
    bool completeLength = false;
    constexpr uint32_t ulebPayloadMask = 0x7FU;
    constexpr uint32_t ulebExtensionBit = 0x80U;
    constexpr size_t maxUleb32Bytes = 5U;
    for (size_t index = 0U; index < maxUleb32Bytes; ++index) {
        if (textOffset >= fileSize) {
            return Result<std::pair<uint64_t, uint32_t>>::Failure(
                InvalidMetadata("string length crosses the file boundary"));
        }
        const auto byte = base[textOffset++];
        if (index == maxUleb32Bytes - 1U && (byte & 0xF0U) != 0U) {
            return Result<std::pair<uint64_t, uint32_t>>::Failure(
                InvalidMetadata("string has a malformed UTF-16 length"));
        }
        taggedUtf16Length |= static_cast<uint32_t>(byte & ulebPayloadMask) << (index * 7U);
        if ((byte & ulebExtensionBit) == 0U) {
            completeLength = true;
            break;
        }
    }
    if (!completeLength) {
        return Result<std::pair<uint64_t, uint32_t>>::Failure(
            InvalidMetadata("string has an unterminated UTF-16 length"));
    }
    return Result<std::pair<uint64_t, uint32_t>>::Success({textOffset, taggedUtf16Length >> 1U});
}

}  // namespace

Result<MetadataStringRead> ReadMetadataString(const panda_file::File &file, uint64_t offset, size_t maxTextBytes);

// Shared state for the ordered MetadataCatalog build pipeline. The inputs, the working
// budget, the lookup maps and the catalog under construction live here. Private
// MetadataCatalog state is touched only through the friend methods below, and the
// private nested observer type is wrapped by the counting methods so that the free build
// helpers never need to name it.
struct CatalogBuilderContext {
    const panda_file::File *file {nullptr};
    const PhysicalProgram *physical {nullptr};
    const StructureIndex *structure {nullptr};
    MetadataCatalog::BuildObserver *observer {nullptr};
    uint64_t fileSize {0U};
    std::shared_ptr<MetadataCatalog> catalog;
    catalog_build_detail::BuildBudget budget {0U};
    std::map<PhysicalBackingKey, catalog_build_detail::BackingInfo> entitiesByBacking;
    std::map<MetadataObjectKey, size_t> objectIndex;
    std::set<uint64_t> invalidStringOffsets;
    std::set<std::string> diagnosticIndex;
    bool stopOptionalEvidence {false};
    bool stopReferences {false};
    bool diagnosticAdmissionExhausted {false};
    std::optional<Status> fatalError;
    size_t performedStringDecodes {0U};

    void MarkPartial()
    {
        catalog->partial_ = true;
    }

    bool IsPartial() const
    {
        return catalog->partial_;
    }

    std::vector<MetadataObject> &Objects()
    {
        return catalog->objects_;
    }

    const std::vector<MetadataObject> &Objects() const
    {
        return catalog->objects_;
    }

    std::vector<MetadataReference> &StructuralReferences()
    {
        return catalog->structuralReferences_;
    }

    const std::vector<MetadataReference> &StructuralReferences() const
    {
        return catalog->structuralReferences_;
    }

    std::map<uint64_t, std::vector<EntityId>> &MethodEntities()
    {
        return catalog->methodEntities_;
    }

    const std::map<uint64_t, std::vector<EntityId>> &MethodEntities() const
    {
        return catalog->methodEntities_;
    }

    std::vector<std::string> &Diagnostics()
    {
        return catalog->diagnostics_;
    }

    const std::vector<std::string> &Diagnostics() const
    {
        return catalog->diagnostics_;
    }

    void SetEstimatedBytes(uint64_t bytes)
    {
        catalog->estimatedBytes_ = bytes;
    }

    void CountRequiredValidationReferenceVisit() const
    {
        if (observer != nullptr) {
            ++observer->requiredValidationReferenceVisits;
        }
    }

    void CountDiagnosticAdmissionCheck() const
    {
        if (observer != nullptr) {
            ++observer->diagnosticAdmissionChecks;
        }
    }

    void CountStringDecode() const
    {
        if (observer != nullptr) {
            ++observer->stringDecodeCount;
        }
    }

    void CountReferenceUpdate() const
    {
        if (observer != nullptr) {
            ++observer->referenceCountUpdates;
        }
    }

    void SetPeakConstructionBytes(uint64_t bytes) const
    {
        if (observer != nullptr) {
            observer->peakConstructionBytes = bytes;
        }
    }
};

namespace {

// Validates that every structure backing range fits inside the file.
std::optional<Status> ValidateBackingRanges(const CatalogBuilderContext &context)
{
    std::optional<Status> invalidBacking;
    context.structure->ForEachNode([&invalidBacking, &context](const EntityNode &node) {
        for (const auto &backing : node.backing) {
            if (!invalidBacking.has_value() && !backing.range.IsValidFor(context.fileSize)) {
                invalidBacking =
                    InvalidMetadata(OffsetDiagnostic("Structure backing range is invalid", backing.range.offset));
            }
        }
    });
    return invalidBacking;
}

// Validates that the required String inventory is sorted, unique and in-file, and that
// every representative entry matches a real required-String reference.
std::optional<Status> ValidateRequiredStringInventory(const CatalogBuilderContext &context)
{
    const auto &inventory = context.physical->requiredStringInventory;
    for (size_t index = 0U; index < inventory.size(); ++index) {
        const auto &entry = inventory[index];
        if (index != 0U && inventory[index - 1U].offset >= entry.offset) {
            return InvalidMetadata("required String inventory must be sorted and unique");
        }
        if (entry.offset >= context.fileSize) {
            return InvalidMetadata(OffsetDiagnostic("required metadata target is outside the file", entry.offset));
        }
        if (entry.representativeReferenceIndex >= context.physical->metadataReferences.size()) {
            return InvalidMetadata("required String inventory representative is outside the reference vector");
        }
        const auto &representative = context.physical->metadataReferences[entry.representativeReferenceIndex];
        if (representative.targetKind != PhysicalMetadataTargetKind::STRING ||
            !IsRequiredStringEvidence(representative.referenceKind) || representative.targetOffset != entry.offset) {
            return InvalidMetadata("required String inventory representative does not match its entry");
        }
    }
    return std::nullopt;
}

// Returns the inventory position where the given string offset would live.
auto FindRequiredString(const CatalogBuilderContext &context, uint32_t offset)
{
    const auto &inventory = context.physical->requiredStringInventory;
    return std::lower_bound(inventory.begin(), inventory.end(), offset,
                            [](const auto &entry, uint32_t candidate) { return entry.offset < candidate; });
}

// Validates one metadata reference before construction: its source range must fit the
// file and required-String evidence must resolve to a physical inventory entry.
std::optional<Status> ValidateReference(const CatalogBuilderContext &context,
                                        const PhysicalMetadataReference &reference)
{
    if (reference.sourceRange.has_value() && !reference.sourceRange->IsValidFor(context.fileSize)) {
        return InvalidMetadata(OffsetDiagnostic("metadata source range is invalid", reference.sourceRange->offset));
    }
    if (IsRequiredStringEvidence(reference.referenceKind)) {
        context.CountRequiredValidationReferenceVisit();
        if (reference.targetKind != PhysicalMetadataTargetKind::STRING) {
            return InvalidMetadata("required String evidence has a non-String target kind");
        }
        const auto entry = FindRequiredString(context, reference.targetOffset);
        if (entry == context.physical->requiredStringInventory.end() || entry->offset != reference.targetOffset) {
            return InvalidMetadata(OffsetDiagnostic("required String target is absent from the physical inventory",
                                                    reference.targetOffset));
        }
    }
    return std::nullopt;
}

// Validates every program-level and literal-array metadata reference.
std::optional<Status> ValidateAllReferences(const CatalogBuilderContext &context)
{
    for (const auto &reference : context.physical->metadataReferences) {
        if (auto error = ValidateReference(context, reference); error.has_value()) {
            return error;
        }
    }
    for (const auto &literal : context.physical->literalArrays) {
        for (const auto &reference : literal.metadataReferences) {
            if (auto error = ValidateReference(context, reference); error.has_value()) {
                return error;
            }
        }
    }
    return std::nullopt;
}

// Retains one bounded diagnostic in the catalog while the persistent budget allows it.
void AddDiagnostic(CatalogBuilderContext *context, std::string message)
{
    if (context->diagnosticAdmissionExhausted) {
        return;
    }
    message = BoundedText(message);
    const auto temporaryCost = EstimateString(message);
    if (!context->budget.TryWorking(temporaryCost)) {
        context->MarkPartial();
        context->diagnosticAdmissionExhausted = true;
        return;
    }
    context->CountDiagnosticAdmissionCheck();
    if (context->diagnosticIndex.find(message) != context->diagnosticIndex.end()) {
        context->budget.ReleaseWorking(temporaryCost);
        return;
    }
    const auto oldCapacity = context->Diagnostics().capacity();
    // Keep one charged slot available for the mandatory Partial control diagnostic.
    const auto newCapacity = NextVectorCapacity(oldCapacity, context->Diagnostics().size() + 2U);
    const auto capacityCost = (newCapacity - oldCapacity) * sizeof(std::string);
    const auto oldBufferWorking = newCapacity == oldCapacity ? 0U : oldCapacity * sizeof(std::string);
    const auto cost = SaturatingAdd(EstimateDiagnostic(message), capacityCost);
    const auto indexCost = SaturatingAdd(sizeof(std::string) + MAP_NODE_OVERHEAD, EstimateString(message));
    if (!context->budget.Try(cost, SaturatingAdd(indexCost, oldBufferWorking))) {
        context->budget.ReleaseWorking(temporaryCost);
        context->MarkPartial();
        context->diagnosticAdmissionExhausted = true;
        return;
    }
    if (newCapacity != oldCapacity) {
        context->Diagnostics().reserve(newCapacity);
        context->budget.ReleaseWorking(oldBufferWorking);
    }
    context->diagnosticIndex.emplace(message);
    context->Diagnostics().push_back(std::move(message));
    context->budget.ReleaseWorking(temporaryCost);
}

// Registers one entity under a method offset, growing the id list under the budget.
void AddMethodEntity(CatalogBuilderContext *context, uint64_t offset, EntityId entityId)
{
    auto found = context->MethodEntities().find(offset);
    if (found == context->MethodEntities().end()) {
        constexpr uint64_t entryCost =
            sizeof(uint64_t) + 2U * sizeof(std::vector<EntityId>) + 2U * sizeof(EntityId) + MAP_NODE_OVERHEAD;
        if (!context->budget.TryPersistent(entryCost)) {
            context->MarkPartial();
            return;
        }
        std::vector<EntityId> ids;
        ids.reserve(1U);
        ids.push_back(entityId);
        context->MethodEntities().emplace(offset, std::move(ids));
        return;
    }
    auto &ids = found->second;
    if (std::find(ids.begin(), ids.end(), entityId) != ids.end()) {
        return;
    }
    if (ids.size() == ids.capacity()) {
        const auto newCapacity = ids.size() + 1U;
        const auto persistentCost = (newCapacity - ids.capacity()) * sizeof(EntityId);
        const auto reserveWorking = newCapacity * sizeof(EntityId);
        if (!context->budget.Try(persistentCost, reserveWorking)) {
            context->MarkPartial();
            return;
        }
        ids.reserve(newCapacity);
        context->budget.ReleaseWorking(reserveWorking);
    }
    ids.push_back(entityId);
}

// Registers one entity under a physical backing key, growing the id list under the
// working budget.
void AddBackingEntity(CatalogBuilderContext *context, const PhysicalBackingKey &key, EntityId entityId,
                      const std::string &preview)
{
    auto found = context->entitiesByBacking.find(key);
    if (found == context->entitiesByBacking.end()) {
        BackingInfo info;
        info.entityIds.reserve(1U);
        info.entityIds.push_back(entityId);
        if (key.kind == PhysicalEntityKind::METHOD) {
            info.preview = BoundedText(preview);
        }
        const auto cost = EstimateBackingInfo(info);
        if (!context->budget.TryWorking(cost)) {
            context->MarkPartial();
            return;
        }
        context->entitiesByBacking.emplace(key, std::move(info));
        return;
    }
    auto &ids = found->second.entityIds;
    if (std::find(ids.begin(), ids.end(), entityId) != ids.end()) {
        return;
    }
    if (ids.size() == ids.capacity()) {
        const auto newCapacity = ids.size() + 1U;
        const auto workingCost = 2U * (newCapacity - ids.capacity()) * sizeof(EntityId);
        if (!context->budget.TryWorking(workingCost)) {
            context->MarkPartial();
            return;
        }
        ids.reserve(newCapacity);
    }
    ids.push_back(entityId);
}

// Registers every structure node's backing entities, then sorts the accumulated id lists.
void PopulateEntities(CatalogBuilderContext *context)
{
    const auto nodeListWorking =
        SaturatingAdd(MAP_NODE_OVERHEAD, 2U * context->structure->Size() * sizeof(EntityNode *));
    if (!context->budget.TryWorking(nodeListWorking)) {
        context->MarkPartial();
        return;
    }
    const auto nodes = context->structure->NodesInIdOrder();
    for (const auto *node : nodes) {
        for (const auto &backing : node->backing) {
            const PhysicalBackingKey key {backing.kind, backing.range.offset};
            AddBackingEntity(context, key, node->id, node->VisibleName());
            if (backing.kind == PhysicalEntityKind::METHOD) {
                AddMethodEntity(context, backing.range.offset, node->id);
            }
        }
    }
    context->budget.ReleaseWorking(nodeListWorking);
    for (auto &[key, info] : context->entitiesByBacking) {
        (void)key;
        SortUnique(&info.entityIds);
    }
    for (auto &[offset, entityIds] : context->MethodEntities()) {
        (void)offset;
        SortUnique(&entityIds);
    }
}

// Merges entity ids into an object that already exists, growing its list under the budget.
void AppendObjectEntities(CatalogBuilderContext *context, MetadataObject *object,
                          const std::vector<EntityId> &entityIds)
{
    for (const auto entityId : entityIds) {
        if (std::find(object->entityIds.begin(), object->entityIds.end(), entityId) != object->entityIds.end()) {
            continue;
        }
        if (object->entityIds.size() == object->entityIds.capacity()) {
            const auto newCapacity = object->entityIds.size() + 1U;
            const auto persistentCost = (newCapacity - object->entityIds.capacity()) * sizeof(EntityId);
            const auto reserveWorking = newCapacity * sizeof(EntityId);
            if (!context->budget.Try(persistentCost, reserveWorking)) {
                context->MarkPartial();
                return;
            }
            object->entityIds.reserve(newCapacity);
            context->budget.ReleaseWorking(reserveWorking);
        }
        object->entityIds.push_back(entityId);
    }
}

// Seed of one catalog object insert: its key, navigable range, display preview with the truncation
// flag, origin and source entities.
struct CatalogObjectSeed {
    MetadataObjectKey key;
    std::optional<FileRange> range;
    std::string preview;
    bool truncated {false};
    MetadataOrigin origin {MetadataOrigin::INDEX};
    std::vector<EntityId> entityIds;
};

// Adds or resolves one metadata object from its seed, charging the persistent and working budget
// for the vector growth and index node it requires.
std::optional<size_t> AddObject(CatalogBuilderContext *context, const CatalogObjectSeed &seed)
{
    if (const auto found = context->objectIndex.find(seed.key); found != context->objectIndex.end()) {
        auto &object = context->Objects()[found->second];
        if (std::find(object.origins.begin(), object.origins.end(), seed.origin) == object.origins.end()) {
            object.origins.push_back(seed.origin);
        }
        AppendObjectEntities(context, &object, seed.entityIds);
        return found->second;
    }

    const auto previewBytes = std::min(seed.preview.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    const auto temporaryCost = SaturatingAdd(2U * sizeof(MetadataObject) + MAP_NODE_OVERHEAD + previewBytes,
                                             2U * seed.entityIds.size() * sizeof(EntityId));
    if (!context->budget.TryWorking(temporaryCost)) {
        context->MarkPartial();
        context->stopOptionalEvidence = true;
        return std::nullopt;
    }
    MetadataObject object {seed.key, seed.range, BoundedText(seed.preview), seed.truncated, {}, seed.entityIds, {}, 0U};
    object.origins.reserve(4U);
    object.origins.push_back(seed.origin);
    SortUnique(&object.entityIds);
    const auto oldCapacity = context->Objects().capacity();
    const auto newCapacity = NextVectorCapacity(oldCapacity, context->Objects().size() + 1U);
    const auto capacityCost = (newCapacity - oldCapacity) * sizeof(MetadataObject);
    const auto oldBufferWorking = newCapacity == oldCapacity ? 0U : oldCapacity * sizeof(MetadataObject);
    const auto persistentCost = SaturatingAdd(EstimateObject(object), capacityCost);
    if (!context->budget.Try(persistentCost, SaturatingAdd(OBJECT_INDEX_WORKING_BYTES, oldBufferWorking))) {
        context->budget.ReleaseWorking(temporaryCost);
        context->MarkPartial();
        context->stopOptionalEvidence = true;
        return std::nullopt;
    }
    if (newCapacity != oldCapacity) {
        context->Objects().reserve(newCapacity);
        context->budget.ReleaseWorking(oldBufferWorking);
    }
    const auto index = context->Objects().size();
    context->Objects().push_back(std::move(object));
    context->objectIndex.emplace(seed.key, index);
    context->budget.ReleaseWorking(temporaryCost);
    return index;
}

// Adds the method object for one record method, reusing the structure preview when
// present. Returns a terminal status when the method range is invalid.
std::optional<Status> AddMethodObject(CatalogBuilderContext *context, uint32_t offset, uint64_t size,
                                      const std::string &name)
{
    const FileRange range {offset, size};
    if (offset >= context->fileSize || !range.IsValidFor(context->fileSize)) {
        return InvalidMetadata(OffsetDiagnostic("method range is invalid", offset));
    }
    const PhysicalBackingKey backingKey {PhysicalEntityKind::METHOD, offset};
    static const std::vector<EntityId> EMPTY;
    const auto backing = context->entitiesByBacking.find(backingKey);
    const auto &entityIds = backing == context->entitiesByBacking.end() ? EMPTY : backing->second.entityIds;
    const auto &preview =
        backing == context->entitiesByBacking.end() || backing->second.preview.empty() ? name : backing->second.preview;
    (void)AddObject(context, {{MetadataKind::METHOD, offset}, range, preview, false, MetadataOrigin::INDEX, entityIds});
    return std::nullopt;
}

// Adds the method objects for every record method. Returns a terminal status when a
// record or method range is invalid.
std::optional<Status> AddRecordMethods(CatalogBuilderContext *context)
{
    for (const auto &record : context->physical->records) {
        const FileRange recordRange {record.offset, record.size};
        if (!recordRange.IsValidFor(context->fileSize)) {
            return InvalidMetadata(OffsetDiagnostic("record range is invalid", record.offset));
        }
        for (const auto &method : record.methods) {
            if (auto error = AddMethodObject(context, method.offset, method.size, method.name); error.has_value()) {
                return error;
            }
        }
    }
    return std::nullopt;
}

// Decodes one metadata string into an object, deduplicating and recording invalid
// offsets. Sets the terminal fatal error for failed required targets.
std::optional<size_t> DecodeString(CatalogBuilderContext *context, uint64_t offset, bool required,
                                   MetadataOrigin origin)
{
    const MetadataObjectKey key {MetadataKind::STRING, offset};
    if (const auto found = context->objectIndex.find(key); found != context->objectIndex.end()) {
        auto &origins = context->Objects()[found->second].origins;
        if (std::find(origins.begin(), origins.end(), origin) == origins.end()) {
            origins.push_back(origin);
        }
        return found->second;
    }
    if (context->invalidStringOffsets.find(offset) != context->invalidStringOffsets.end()) {
        if (required) {
            context->fatalError = InvalidMetadata(OffsetDiagnostic("required string target is invalid", offset));
        }
        return std::nullopt;
    }
    if (context->stopOptionalEvidence || !context->budget.TryWorking(STRING_READ_WORKING_BYTES)) {
        context->MarkPartial();
        context->stopOptionalEvidence = true;
        return std::nullopt;
    }
    ++context->performedStringDecodes;
    context->CountStringDecode();
    auto value = ReadMetadataString(*context->file, offset, MetadataLimits::MAX_PREVIEW_BYTES);
    if (!value.HasValue()) {
        context->budget.ReleaseWorking(STRING_READ_WORKING_BYTES);
        if (context->budget.TryWorking(INVALID_STRING_WORKING_BYTES)) {
            context->invalidStringOffsets.insert(offset);
        } else {
            context->MarkPartial();
            context->stopOptionalEvidence = true;
        }
        if (required) {
            context->fatalError = value.Error();
        } else {
            AddDiagnostic(context, OffsetDiagnostic("Ignored invalid optional string target", offset));
        }
        return std::nullopt;
    }
    auto result =
        AddObject(context, {key, value.Value().range, value.Value().text, value.Value().truncated, origin, {}});
    context->budget.ReleaseWorking(STRING_READ_WORKING_BYTES);
    return result;
}

// Ensures every required String inventory entry resolves to a decodable string. Returns
// the terminal fatal error on the first entry that fails.
std::optional<Status> ValidateRequiredStrings(CatalogBuilderContext *context)
{
    for (const auto &entry : context->physical->requiredStringInventory) {
        const auto offset = entry.offset;
        if (context->objectIndex.find({MetadataKind::STRING, offset}) != context->objectIndex.end()) {
            continue;
        }
        const auto previousDecodeCount = context->performedStringDecodes;
        (void)DecodeString(context, offset, true, MetadataOrigin::STRUCTURE);
        if (context->fatalError.has_value()) {
            return context->fatalError;
        }
        if (context->performedStringDecodes != previousDecodeCount) {
            continue;
        }
        ++context->performedStringDecodes;
        context->CountStringDecode();
        auto validation = ReadMetadataString(*context->file, offset, 0U);
        if (!validation.HasValue()) {
            context->fatalError = validation.Error();
            return context->fatalError;
        }
    }
    return std::nullopt;
}

// Adds the method objects referenced by one literal array, resolving names through the
// string decoder when the physical name is empty.
void AddLiteralMethods(CatalogBuilderContext *context, const PhysicalLiteralArray &literal)
{
    for (const auto &method : literal.methods) {
        if (context->objectIndex.find({MetadataKind::METHOD, method.offset}) != context->objectIndex.end()) {
            continue;
        }
        try {
            panda_file::MethodDataAccessor accessor(*context->file, panda_file::File::EntityId(method.offset));
            std::string methodName = BoundedText(method.name);
            auto nameResolved = !methodName.empty();
            if (!nameResolved) {
                const auto name =
                    DecodeString(context, accessor.GetNameId().GetOffset(), false, MetadataOrigin::LITERAL);
                nameResolved = name.has_value();
                methodName = nameResolved ? context->Objects()[*name].preview : std::string();
            }
            if (!nameResolved) {
                AddDiagnostic(context, OffsetDiagnostic("Ignored literal method with invalid name", method.offset));
                continue;
            }
            if (auto error = AddMethodObject(context, method.offset, accessor.GetSize(), methodName);
                error.has_value()) {
                AddDiagnostic(context, error->Message());
            }
        } catch (const std::exception &) {
            AddDiagnostic(context, OffsetDiagnostic("Ignored invalid literal method reference", method.offset));
        }
    }
}

// Adds the literal-array objects and their referenced methods. Records a terminal fatal
// error when a literal array range is invalid so the caller can fail the build.
void AddLiteralArrays(CatalogBuilderContext *context)
{
    for (const auto &literal : context->physical->literalArrays) {
        if (literal.range.offset >= context->fileSize || !literal.range.IsValidFor(context->fileSize)) {
            context->fatalError =
                InvalidMetadata(OffsetDiagnostic("literal array range is invalid", literal.range.offset));
            return;
        }
        static const std::vector<EntityId> EMPTY;
        const auto backing = context->entitiesByBacking.find({PhysicalEntityKind::LITERAL_ARRAY, literal.range.offset});
        const auto &entityIds = backing == context->entitiesByBacking.end() ? EMPTY : backing->second.entityIds;
        const auto navigableRange = literal.range.size == 0U ? std::nullopt : std::make_optional(literal.range);
        (void)AddObject(context, {{MetadataKind::LITERAL_ARRAY, literal.range.offset},
                                  navigableRange,
                                  "literal array",
                                  false,
                                  MetadataOrigin::INDEX,
                                  entityIds});
        if (context->stopOptionalEvidence) {
            continue;
        }
        AddLiteralMethods(context, literal);
    }
}

// Resolves the target object index for one metadata reference, decoding strings and
// reporting inventory misses. Sets stopReferences when optional decoding stopped.
std::optional<size_t> ResolveReferenceTarget(CatalogBuilderContext *context,
                                             const PhysicalMetadataReference &physicalReference,
                                             const MetadataObjectKey &key)
{
    if (key.kind == MetadataKind::STRING) {
        auto targetIndex = DecodeString(context, key.offset, IsRequiredStringEvidence(physicalReference.referenceKind),
                                        OriginFor(physicalReference.referenceKind));
        if (!targetIndex.has_value() && context->stopOptionalEvidence) {
            context->stopReferences = true;
        }
        return targetIndex;
    }
    if (const auto found = context->objectIndex.find(key); found != context->objectIndex.end()) {
        return found->second;
    }
    AddDiagnostic(context, OffsetDiagnostic("Ignored metadata target missing from physical inventory", key.offset));
    return std::nullopt;
}

// Locates the entity ids that source one metadata reference, by method offset or by
// the backing range of its source kind.
const std::vector<EntityId> &SourceEntityIds(const CatalogBuilderContext &context,
                                             const PhysicalMetadataReference &physicalReference)
{
    static const std::vector<EntityId> EMPTY;
    const std::vector<EntityId> *sourceEntityIds = &EMPTY;
    if (physicalReference.sourceMethodOffset.has_value()) {
        if (const auto found = context.MethodEntities().find(*physicalReference.sourceMethodOffset);
            found != context.MethodEntities().end()) {
            sourceEntityIds = &found->second;
        }
    } else if (physicalReference.sourceRange.has_value()) {
        const PhysicalBackingKey sourceKey {SourceEntityKind(physicalReference.referenceKind),
                                            physicalReference.sourceRange->offset};
        if (const auto found = context.entitiesByBacking.find(sourceKey); found != context.entitiesByBacking.end()) {
            sourceEntityIds = &found->second.entityIds;
        }
    }
    return *sourceEntityIds;
}

// Builds the public reference model from one physical metadata reference.
MetadataReference BuildStructuralReference(const PhysicalMetadataReference &physicalReference,
                                           const MetadataObjectKey &key, const std::vector<EntityId> &sourceEntityIds)
{
    MetadataReference reference;
    reference.target = key;
    reference.kind = PublicReferenceKind(physicalReference.referenceKind);
    reference.sourceMethodOffset = physicalReference.sourceMethodOffset;
    reference.sourceRange = physicalReference.sourceRange.has_value() && physicalReference.sourceRange->size > 0U
                                ? physicalReference.sourceRange
                                : std::nullopt;
    reference.sourceName = BoundedText(physicalReference.sourceName);
    reference.sourceEntityIds = sourceEntityIds;
    if (physicalReference.referenceKind == PhysicalMetadataReferenceKind::INSTRUCTION_EVIDENCE ||
        physicalReference.referenceKind == PhysicalMetadataReferenceKind::LITERAL_STRING ||
        physicalReference.referenceKind == PhysicalMetadataReferenceKind::LITERAL_METHOD ||
        physicalReference.referenceKind == PhysicalMetadataReferenceKind::NESTED_LITERAL_ARRAY) {
        reference.operandIndex = physicalReference.ordinal;
    }
    return reference;
}

// Charges the budget for one resolved reference and appends it, merging its source
// entities into the target object.
void AppendStructuralReference(CatalogBuilderContext *context, size_t targetIndex, MetadataReference reference,
                               uint64_t temporaryCost, MetadataOrigin origin)
{
    auto &target = context->Objects()[targetIndex];
    size_t additionalEntityCount = 0U;
    for (const auto entityId : reference.sourceEntityIds) {
        if (std::find(target.entityIds.begin(), target.entityIds.end(), entityId) == target.entityIds.end()) {
            ++additionalEntityCount;
        }
    }
    const auto requiredCapacity = target.entityIds.size() + additionalEntityCount;
    const auto growsEntityIds = requiredCapacity > target.entityIds.capacity();
    const auto persistentEntityCost =
        growsEntityIds ? (requiredCapacity - target.entityIds.capacity()) * sizeof(EntityId) : 0U;
    const auto entityReserveWorking = growsEntityIds ? requiredCapacity * sizeof(EntityId) : 0U;
    const auto oldReferenceCapacity = context->StructuralReferences().capacity();
    const auto newReferenceCapacity =
        NextVectorCapacity(oldReferenceCapacity, context->StructuralReferences().size() + 1U);
    const auto referenceCapacityCost = (newReferenceCapacity - oldReferenceCapacity) * sizeof(MetadataReference);
    const auto oldReferenceBufferWorking =
        newReferenceCapacity == oldReferenceCapacity ? 0U : oldReferenceCapacity * sizeof(MetadataReference);
    const auto referenceCost =
        SaturatingAdd(EstimateReference(reference), SaturatingAdd(persistentEntityCost, referenceCapacityCost));
    if (!context->budget.Try(referenceCost, SaturatingAdd(entityReserveWorking, oldReferenceBufferWorking))) {
        context->budget.ReleaseWorking(temporaryCost);
        context->MarkPartial();
        context->stopReferences = true;
        return;
    }
    if (growsEntityIds) {
        target.entityIds.reserve(requiredCapacity);
        context->budget.ReleaseWorking(entityReserveWorking);
    }
    if (newReferenceCapacity != oldReferenceCapacity) {
        context->StructuralReferences().reserve(newReferenceCapacity);
        context->budget.ReleaseWorking(oldReferenceBufferWorking);
    }
    for (const auto entityId : reference.sourceEntityIds) {
        if (std::find(target.entityIds.begin(), target.entityIds.end(), entityId) == target.entityIds.end()) {
            target.entityIds.push_back(entityId);
        }
    }
    context->StructuralReferences().push_back(std::move(reference));
    if (std::find(target.origins.begin(), target.origins.end(), origin) == target.origins.end()) {
        target.origins.push_back(origin);
    }
    ++target.referenceCount;
    context->CountReferenceUpdate();
    context->budget.ReleaseWorking(temporaryCost);
}

// Processes one physical metadata reference into the catalog, stopping all reference
// work once the budget or optional evidence is exhausted.
void ProcessReference(CatalogBuilderContext *context, const PhysicalMetadataReference &physicalReference)
{
    if (context->stopReferences || context->fatalError.has_value()) {
        return;
    }
    if (physicalReference.targetOffset >= context->fileSize) {
        AddDiagnostic(context, OffsetDiagnostic("Ignored out-of-file metadata target", physicalReference.targetOffset));
        return;
    }
    const MetadataObjectKey key {PublicKind(physicalReference.targetKind), physicalReference.targetOffset};
    const auto targetIndex = ResolveReferenceTarget(context, physicalReference, key);
    if (context->fatalError.has_value() || !targetIndex.has_value()) {
        return;
    }
    const auto &sourceEntityIds = SourceEntityIds(*context, physicalReference);
    const auto sourceNameBytes = std::min(physicalReference.sourceName.size(), MetadataLimits::MAX_PREVIEW_BYTES);
    const auto temporaryCost = SaturatingAdd(2U * sizeof(MetadataReference) + MAP_NODE_OVERHEAD + sourceNameBytes,
                                             2U * sourceEntityIds.size() * sizeof(EntityId));
    if (!context->budget.TryWorking(temporaryCost)) {
        context->MarkPartial();
        context->stopReferences = true;
        return;
    }
    auto reference = BuildStructuralReference(physicalReference, key, sourceEntityIds);
    AppendStructuralReference(context, *targetIndex, std::move(reference), temporaryCost,
                              OriginFor(physicalReference.referenceKind));
}

// Processes every program-level and literal-array metadata reference in order.
void ProcessReferences(CatalogBuilderContext *context)
{
    for (const auto &reference : context->physical->metadataReferences) {
        ProcessReference(context, reference);
    }
    for (const auto &literal : context->physical->literalArrays) {
        for (const auto &reference : literal.metadataReferences) {
            ProcessReference(context, reference);
        }
    }
}

// Sorts and deduplicates the accumulated catalog state, appends the partial control
// diagnostic when needed, and returns the finished catalog.
Result<std::shared_ptr<const MetadataCatalog>> FinalizeCatalog(CatalogBuilderContext *context)
{
    for (auto &object : context->Objects()) {
        SortUnique(&object.origins);
        SortUnique(&object.entityIds);
        SortUnique(&object.diagnostics);
    }
    std::sort(context->Objects().begin(), context->Objects().end(),
              [](const auto &left, const auto &right) { return left.key < right.key; });
    const auto byStructuralReference = [](const auto &left, const auto &right) {
        const uint64_t leftSource =
            left.sourceRange.has_value() ? left.sourceRange->offset : left.sourceMethodOffset.value_or(0U);
        const uint64_t rightSource =
            right.sourceRange.has_value() ? right.sourceRange->offset : right.sourceMethodOffset.value_or(0U);
        return std::tie(left.target, left.kind, leftSource, left.operandIndex, left.sourceName) <
               std::tie(right.target, right.kind, rightSource, right.operandIndex, right.sourceName);
    };
    std::sort(context->StructuralReferences().begin(), context->StructuralReferences().end(), byStructuralReference);
    SortUnique(&context->Diagnostics());

    if (context->IsPartial()) {
        // The required state diagnostic is control-plane data rather than retained evidence. It remains available
        // even when the caller deliberately supplies a budget smaller than one std::string, as in the 1-byte test.
        constexpr const char *PARTIAL_DIAGNOSTIC =
            "Metadata catalog reached its persistent-memory budget and is partial";
        if (!context->Diagnostics().empty() && context->Diagnostics().size() == context->Diagnostics().capacity()) {
            context->Diagnostics().back() = PARTIAL_DIAGNOSTIC;
        } else {
            context->Diagnostics().push_back(PARTIAL_DIAGNOSTIC);
        }
        SortUnique(&context->Diagnostics());
    }
    context->SetEstimatedBytes(context->budget.PersistentBytes());
    context->SetPeakConstructionBytes(context->budget.PeakBytes());
    return Result<std::shared_ptr<const MetadataCatalog>>::Success(std::move(context->catalog));
}

}  // namespace

Result<MetadataStringRead> ReadMetadataString(const panda_file::File &file, uint64_t offset, size_t maxTextBytes)
{
    const uint64_t fileSize = file.GetHeader()->file_size;
    const auto header = ReadMutf8LengthPrefix(file, offset);
    if (!header.HasValue()) {
        return Result<MetadataStringRead>::Failure(header.Error());
    }
    const auto [textOffset, expectedUtf16Length] = header.Value();
    const auto *base = file.GetBase();

    uint64_t terminatorOffset = textOffset;
    while (terminatorOffset < fileSize && base[terminatorOffset] != 0U) {
        ++terminatorOffset;
    }
    if (terminatorOffset >= fileSize) {
        return Result<MetadataStringRead>::Failure(InvalidMetadata("unterminated MUTF-8 string"));
    }

    MetadataStringRead result;
    result.range = {offset, terminatorOffset - offset + 1U};
    const auto textSize = static_cast<size_t>(terminatorOffset - textOffset);
    result.text.reserve(std::min(maxTextBytes, textSize));
    size_t position = 0U;
    uint32_t utf16Length = 0U;
    bool retainingText = true;
    while (position < textSize) {
        auto decoded = DecodeMutf8CodePoint(base + textOffset + position, textSize - position);
        if (!decoded.HasValue()) {
            return Result<MetadataStringRead>::Failure(decoded.Error());
        }
        const auto &point = decoded.Value();
        if (point.utf16Units > expectedUtf16Length - std::min(utf16Length, expectedUtf16Length)) {
            return Result<MetadataStringRead>::Failure(InvalidMetadata("string UTF-16 length does not match its data"));
        }
        utf16Length += point.utf16Units;
        if (retainingText && !AppendUtf8(point.codePoint, maxTextBytes, &result.text)) {
            retainingText = false;
            result.truncated = true;
        }
        position += point.consumed;
    }
    if (utf16Length != expectedUtf16Length) {
        return Result<MetadataStringRead>::Failure(InvalidMetadata("string UTF-16 length does not match its data"));
    }
    return Result<MetadataStringRead>::Success(std::move(result));
}

Result<std::shared_ptr<const MetadataCatalog>> MetadataCatalog::Build(const panda_file::File &file,
                                                                      const PhysicalProgram &physical,
                                                                      const StructureIndex &structure,
                                                                      MetadataLimits limits)
{
    return BuildImpl(file, physical, structure, limits, nullptr);
}

Result<std::shared_ptr<const MetadataCatalog>> MetadataCatalog::BuildImpl(const panda_file::File &file,
                                                                          const PhysicalProgram &physical,
                                                                          const StructureIndex &structure,
                                                                          MetadataLimits limits,
                                                                          BuildObserver *observer)
{
    CatalogBuilderContext context;
    context.file = &file;
    context.physical = &physical;
    context.structure = &structure;
    context.observer = observer;
    context.fileSize = file.GetHeader()->file_size;
    if (physical.fileSize != context.fileSize) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(
            InvalidMetadata("physical file size does not match the ABC header"));
    }
    if (auto error = ValidateBackingRanges(context); error.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*error));
    }
    if (auto error = ValidateRequiredStringInventory(context); error.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*error));
    }
    if (auto error = ValidateAllReferences(context); error.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*error));
    }
    context.catalog = std::make_shared<MetadataCatalog>();
    context.budget = BuildBudget(limits.maxPersistentBytes);

    PopulateEntities(&context);

    if (auto error = AddRecordMethods(&context); error.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*error));
    }

    if (auto error = ValidateRequiredStrings(&context); error.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*error));
    }

    AddLiteralArrays(&context);
    if (context.fatalError.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*context.fatalError));
    }

    context.stopReferences = context.stopOptionalEvidence;
    ProcessReferences(&context);
    if (context.fatalError.has_value()) {
        return Result<std::shared_ptr<const MetadataCatalog>>::Failure(std::move(*context.fatalError));
    }
    return FinalizeCatalog(&context);
}

const MetadataObject *MetadataCatalog::Find(const MetadataObjectKey &key) const
{
    const auto found =
        std::lower_bound(objects_.begin(), objects_.end(), key,
                         [](const auto &object, const auto &candidate) { return object.key < candidate; });
    return found != objects_.end() && found->key == key ? &*found : nullptr;
}

const std::vector<EntityId> &MetadataCatalog::EntitiesForMethod(uint64_t methodOffset) const
{
    static const std::vector<EntityId> EMPTY;
    const auto found = methodEntities_.find(methodOffset);
    return found == methodEntities_.end() ? EMPTY : found->second;
}

}  // namespace panda::bytecode_explorer
