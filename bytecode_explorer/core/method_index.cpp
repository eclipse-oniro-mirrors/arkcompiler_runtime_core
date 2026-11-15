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

#include "core/method_index.h"

#include "libpandafile/file.h"
#include "method_data_accessor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

constexpr uint64_t METHOD_FIXED_HEADER_SIZE = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
constexpr uint64_t FIELD_FIXED_HEADER_SIZE = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);
constexpr uint64_t MAX_ULEB32_SIZE = 5U;

bool IsRangeInside(uint64_t offset, uint64_t amount, uint64_t size)
{
    return offset <= size && amount <= size - offset;
}

bool Advance(uint64_t amount, uint64_t size, uint64_t *cursor)
{
    if (!IsRangeInside(*cursor, amount, size)) {
        return false;
    }
    *cursor += amount;
    return true;
}

bool ReadUleb32(const uint8_t *data, uint64_t size, uint64_t *cursor, uint32_t *value)
{
    uint32_t result = 0U;
    for (uint64_t byteIndex = 0U; byteIndex < MAX_ULEB32_SIZE; ++byteIndex) {
        if (*cursor >= size) {
            return false;
        }
        const auto byte = data[static_cast<size_t>((*cursor)++)];
        if (byteIndex == MAX_ULEB32_SIZE - 1U && (byte & 0xF0U) != 0U) {
            return false;
        }
        result |= static_cast<uint32_t>(byte & 0x7FU) << (byteIndex * 7U);
        if ((byte & 0x80U) == 0U) {
            *value = result;
            return true;
        }
    }
    return false;
}

bool SkipLeb32(const uint8_t *data, uint64_t size, uint64_t *cursor)
{
    for (uint64_t byteIndex = 0U; byteIndex < MAX_ULEB32_SIZE; ++byteIndex) {
        if (*cursor >= size) {
            return false;
        }
        const auto byte = data[static_cast<size_t>((*cursor)++)];
        if ((byte & 0x80U) == 0U) {
            return byteIndex != MAX_ULEB32_SIZE - 1U || byte <= 0x07U || byte >= 0x78U;
        }
    }
    return false;
}

uint16_t ReadUint16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t ReadUint32(const uint8_t *data)
{
    uint32_t value = 0U;
    uint32_t shift = 0U;
    for (size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<uint32_t>(data[index]) << shift;
        shift += 8U;
    }
    return value;
}

bool IsIndexTableInsideFile(uint32_t offset, uint32_t count, uint64_t fileSize)
{
    const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(panda_file::File::EntityId);
    return IsRangeInside(offset, bytes, fileSize);
}

bool ValidateMethodHeader(const panda_file::File &file, uint64_t methodOffset)
{
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (methodOffset > std::numeric_limits<uint32_t>::max() ||
        !IsRangeInside(methodOffset, METHOD_FIXED_HEADER_SIZE, fileSize)) {
        return false;
    }
    const panda_file::File::EntityId methodId(static_cast<uint32_t>(methodOffset));
    const auto *indexHeader = file.GetIndexHeader(methodId);
    if (!methodId.IsValid() || indexHeader == nullptr || indexHeader->start > methodOffset ||
        methodOffset >= indexHeader->end || indexHeader->end > fileSize ||
        !IsIndexTableInsideFile(indexHeader->class_idx_off, indexHeader->class_idx_size, fileSize) ||
        (indexHeader->proto_idx_size != std::numeric_limits<uint32_t>::max() &&
         !IsIndexTableInsideFile(indexHeader->proto_idx_off, indexHeader->proto_idx_size, fileSize))) {
        return false;
    }

    const auto *method = file.GetBase() + static_cast<size_t>(methodOffset);  // NOLINT
    const auto classIndex = ReadUint16(method);
    const auto protoIndex = ReadUint16(method + sizeof(panda_file::File::Index));
    return classIndex < indexHeader->class_idx_size &&
           (protoIndex == std::numeric_limits<panda_file::File::Index>::max() ||
            (indexHeader->proto_idx_size != std::numeric_limits<uint32_t>::max() &&
             protoIndex < indexHeader->proto_idx_size));
}

bool FailWithDiagnostic(std::string *diagnostic, const char *message)
{
    if (diagnostic != nullptr && diagnostic->empty()) {
        *diagnostic = message;
    }
    return false;
}

enum class TaggedEntity { CLASS, METHOD, FIELD };

const char *OutOfOrderDiagnostic(TaggedEntity entity)
{
    switch (entity) {
        case TaggedEntity::CLASS:
            return "CLASS tagged sections are out of order";
        case TaggedEntity::METHOD:
            return "METHOD tagged sections are out of order";
        case TaggedEntity::FIELD:
            return "FIELD tagged sections are out of order";
    }
    UNREACHABLE();
}

const char *DuplicateDiagnostic(TaggedEntity entity)
{
    switch (entity) {
        case TaggedEntity::CLASS:
            return "CLASS optional tag is duplicated";
        case TaggedEntity::METHOD:
            return "METHOD optional tag is duplicated";
        case TaggedEntity::FIELD:
            return "FIELD optional tag is duplicated";
    }
    UNREACHABLE();
}

class TaggedSectionTracker final {
public:
    bool Accept(uint8_t tag, bool repeatable, TaggedEntity entity, std::string *diagnostic)
    {
        if (seen_ && tag < lastTag_) {
            return FailWithDiagnostic(diagnostic, OutOfOrderDiagnostic(entity));
        }
        if (seen_ && tag == lastTag_ && !repeatable) {
            return FailWithDiagnostic(diagnostic, DuplicateDiagnostic(entity));
        }
        seen_ = true;
        lastTag_ = tag;
        return true;
    }

private:
    uint8_t lastTag_ {0U};
    bool seen_ {false};
};

std::optional<bool> IsRepeatable(panda_file::ClassTag tag)
{
    switch (tag) {
        case panda_file::ClassTag::INTERFACES:
        case panda_file::ClassTag::SOURCE_LANG:
        case panda_file::ClassTag::SOURCE_FILE:
            return false;
        case panda_file::ClassTag::RUNTIME_ANNOTATION:
        case panda_file::ClassTag::ANNOTATION:
        case panda_file::ClassTag::RUNTIME_TYPE_ANNOTATION:
        case panda_file::ClassTag::TYPE_ANNOTATION:
            return true;
        case panda_file::ClassTag::NOTHING:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<bool> IsRepeatable(panda_file::MethodTag tag)
{
    switch (tag) {
        case panda_file::MethodTag::CODE:
        case panda_file::MethodTag::SOURCE_LANG:
        case panda_file::MethodTag::RUNTIME_PARAM_ANNOTATION:
        case panda_file::MethodTag::DEBUG_INFO:
        case panda_file::MethodTag::PARAM_ANNOTATION:
            return false;
        case panda_file::MethodTag::RUNTIME_ANNOTATION:
        case panda_file::MethodTag::ANNOTATION:
        case panda_file::MethodTag::TYPE_ANNOTATION:
        case panda_file::MethodTag::RUNTIME_TYPE_ANNOTATION:
            return true;
        case panda_file::MethodTag::NOTHING:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<bool> IsRepeatable(panda_file::FieldTag tag)
{
    switch (tag) {
        case panda_file::FieldTag::INT_VALUE:
        case panda_file::FieldTag::VALUE:
            return false;
        case panda_file::FieldTag::RUNTIME_ANNOTATION:
        case panda_file::FieldTag::ANNOTATION:
        case panda_file::FieldTag::RUNTIME_TYPE_ANNOTATION:
        case panda_file::FieldTag::TYPE_ANNOTATION:
            return true;
        case panda_file::FieldTag::NOTHING:
            return std::nullopt;
    }
    return std::nullopt;
}

bool SkipClassTags(const uint8_t *data, uint64_t size, uint64_t *cursor, const CancellationToken &cancellation,
                   std::string *diagnostic)
{
    TaggedSectionTracker sections;
    while (*cursor < size) {
        if (cancellation.IsCancellationRequested()) {
            return false;
        }
        const auto tag = static_cast<panda_file::ClassTag>(data[static_cast<size_t>((*cursor)++)]);
        if (tag == panda_file::ClassTag::NOTHING) {
            return true;
        }
        const auto repeatable = IsRepeatable(tag);
        if (!repeatable.has_value()) {
            return FailWithDiagnostic(diagnostic, "CLASS tagged section is unknown");
        }
        if (!sections.Accept(static_cast<uint8_t>(tag), *repeatable, TaggedEntity::CLASS, diagnostic)) {
            return false;
        }
        switch (tag) {
            case panda_file::ClassTag::INTERFACES: {
                uint32_t count = 0U;
                if (!ReadUleb32(data, size, cursor, &count) ||
                    !Advance(static_cast<uint64_t>(count) * sizeof(panda_file::File::Index), size, cursor)) {
                    return FailWithDiagnostic(diagnostic, "CLASS tagged section payload is malformed");
                }
                break;
            }
            case panda_file::ClassTag::SOURCE_LANG:
                if (!Advance(sizeof(panda_file::SourceLang), size, cursor)) {
                    return FailWithDiagnostic(diagnostic, "CLASS tagged section payload is truncated");
                }
                break;
            case panda_file::ClassTag::RUNTIME_ANNOTATION:
            case panda_file::ClassTag::ANNOTATION:
            case panda_file::ClassTag::RUNTIME_TYPE_ANNOTATION:
            case panda_file::ClassTag::TYPE_ANNOTATION:
            case panda_file::ClassTag::SOURCE_FILE:
                if (!Advance(sizeof(panda_file::File::EntityId), size, cursor)) {
                    return FailWithDiagnostic(diagnostic, "CLASS tagged section payload is truncated");
                }
                break;
            case panda_file::ClassTag::NOTHING:
            default:
                return FailWithDiagnostic(diagnostic, "CLASS tagged section is unknown");
        }
    }
    return FailWithDiagnostic(diagnostic, "CLASS tagged sections are unterminated");
}

// Skips the payload of one tagged FIELD section.
bool SkipFieldPayload(panda_file::FieldTag tag, const uint8_t *data, uint64_t size, uint64_t *cursor,
                      std::string *diagnostic)
{
    switch (tag) {
        case panda_file::FieldTag::INT_VALUE:
            if (!SkipLeb32(data, size, cursor)) {
                return FailWithDiagnostic(diagnostic, "FIELD integer value is malformed");
            }
            break;
        case panda_file::FieldTag::VALUE:
        case panda_file::FieldTag::RUNTIME_ANNOTATION:
        case panda_file::FieldTag::ANNOTATION:
        case panda_file::FieldTag::RUNTIME_TYPE_ANNOTATION:
        case panda_file::FieldTag::TYPE_ANNOTATION:
            if (!Advance(sizeof(uint32_t), size, cursor)) {
                return FailWithDiagnostic(diagnostic, "FIELD tagged section payload is truncated");
            }
            break;
        case panda_file::FieldTag::NOTHING:
        default:
            return FailWithDiagnostic(diagnostic, "FIELD tagged section is unknown");
    }
    return true;
}

bool SkipField(const uint8_t *data, uint64_t size, uint64_t *cursor, const CancellationToken &cancellation,
               std::string *diagnostic)
{
    uint32_t ignored = 0U;
    if (!Advance(FIELD_FIXED_HEADER_SIZE, size, cursor) || !ReadUleb32(data, size, cursor, &ignored)) {
        return FailWithDiagnostic(diagnostic, "FIELD header is truncated or malformed");
    }
    TaggedSectionTracker sections;
    std::optional<panda_file::FieldTag> valueTag;
    while (*cursor < size) {
        if (cancellation.IsCancellationRequested()) {
            return false;
        }
        const auto tag = static_cast<panda_file::FieldTag>(data[static_cast<size_t>((*cursor)++)]);
        if (tag == panda_file::FieldTag::NOTHING) {
            return true;
        }
        const auto repeatable = IsRepeatable(tag);
        if (!repeatable.has_value()) {
            return FailWithDiagnostic(diagnostic, "FIELD tagged section is unknown");
        }
        if ((tag == panda_file::FieldTag::INT_VALUE || tag == panda_file::FieldTag::VALUE) && valueTag.has_value() &&
            *valueTag != tag) {
            return FailWithDiagnostic(diagnostic, "FIELD value tags are mutually exclusive");
        }
        if (!sections.Accept(static_cast<uint8_t>(tag), *repeatable, TaggedEntity::FIELD, diagnostic)) {
            return false;
        }
        if (tag == panda_file::FieldTag::INT_VALUE || tag == panda_file::FieldTag::VALUE) {
            valueTag = tag;
        }
        if (!SkipFieldPayload(tag, data, size, cursor, diagnostic)) {
            return false;
        }
    }
    return FailWithDiagnostic(diagnostic, "FIELD tagged sections are unterminated");
}

// Reads one METHOD CODE section, validating its referenced code offset.
bool ReadMethodCodeReference(const panda_file::File &file, uint64_t size, uint64_t *cursor,
                             std::optional<uint32_t> *codeOffset, std::string *diagnostic)
{
    if (!IsRangeInside(*cursor, sizeof(uint32_t), size)) {
        return FailWithDiagnostic(diagnostic, "METHOD CODE payload is truncated");
    }
    const auto rawOffset = ReadUint32(file.GetBase() + static_cast<size_t>(*cursor));  // NOLINT
    if (!panda_file::File::EntityId(rawOffset).IsValid() || rawOffset >= size) {
        return FailWithDiagnostic(diagnostic, "METHOD CODE reference is invalid");
    }
    *codeOffset = rawOffset;
    *cursor += sizeof(uint32_t);
    return true;
}

// Handles one tagged METHOD section, skipping its payload or recording the code offset.
bool ApplyMethodTag(panda_file::MethodTag tag, const panda_file::File &file, uint64_t *cursor,
                    std::optional<uint32_t> *codeOffset, std::string *diagnostic)
{
    const uint64_t size = file.GetHeader()->file_size;
    switch (tag) {
        case panda_file::MethodTag::CODE:
            return ReadMethodCodeReference(file, size, cursor, codeOffset, diagnostic);
        case panda_file::MethodTag::SOURCE_LANG:
            if (!Advance(sizeof(panda_file::SourceLang), size, cursor)) {
                return FailWithDiagnostic(diagnostic, "METHOD SOURCE_LANG payload is truncated");
            }
            break;
        case panda_file::MethodTag::RUNTIME_ANNOTATION:
        case panda_file::MethodTag::RUNTIME_PARAM_ANNOTATION:
        case panda_file::MethodTag::DEBUG_INFO:
        case panda_file::MethodTag::ANNOTATION:
        case panda_file::MethodTag::PARAM_ANNOTATION:
        case panda_file::MethodTag::TYPE_ANNOTATION:
        case panda_file::MethodTag::RUNTIME_TYPE_ANNOTATION:
            if (!Advance(sizeof(panda_file::File::EntityId), size, cursor)) {
                return FailWithDiagnostic(diagnostic, "METHOD tagged section payload is truncated");
            }
            break;
        case panda_file::MethodTag::NOTHING:
        default:
            return FailWithDiagnostic(diagnostic, "METHOD tagged section is unknown");
    }
    return true;
}

bool ReadMethod(const panda_file::File &file, uint64_t *cursor, MethodMetadata *method,
                const CancellationToken &cancellation, std::string *diagnostic)
{
    const uint64_t size = file.GetHeader()->file_size;
    const uint64_t methodOffset = *cursor;
    if (!ValidateMethodHeader(file, methodOffset) || !Advance(METHOD_FIXED_HEADER_SIZE, size, cursor)) {
        return FailWithDiagnostic(diagnostic, "METHOD header is malformed");
    }
    uint32_t ignored = 0U;
    if (!ReadUleb32(file.GetBase(), size, cursor, &ignored)) {
        return FailWithDiagnostic(diagnostic, "METHOD access flags are malformed");
    }

    std::optional<uint32_t> codeOffset;
    TaggedSectionTracker sections;
    while (*cursor < size) {
        if (cancellation.IsCancellationRequested()) {
            return false;
        }
        const auto tag = static_cast<panda_file::MethodTag>(file.GetBase()[static_cast<size_t>((*cursor)++)]);
        if (tag == panda_file::MethodTag::NOTHING) {
            *method = {methodOffset, codeOffset};
            return true;
        }
        const auto repeatable = IsRepeatable(tag);
        if (!repeatable.has_value()) {
            return FailWithDiagnostic(diagnostic, "METHOD tagged section is unknown");
        }
        if (!sections.Accept(static_cast<uint8_t>(tag), *repeatable, TaggedEntity::METHOD, diagnostic)) {
            return false;
        }
        if (!ApplyMethodTag(tag, file, cursor, &codeOffset, diagnostic)) {
            return false;
        }
    }
    return FailWithDiagnostic(diagnostic, "METHOD tagged sections are unterminated");
}

bool ReadClass(const panda_file::File &file, uint64_t classOffset, const CancellationToken &cancellation,
               std::unordered_map<uint64_t, MethodMetadata> *methods, std::string *diagnostic)
{
    const uint64_t size = file.GetHeader()->file_size;
    uint64_t cursor = classOffset;
    uint32_t ignored = 0U;
    uint32_t fieldCount = 0U;
    uint32_t methodCount = 0U;
    if (!ReadUleb32(file.GetBase(), size, &cursor, &ignored)) {
        return FailWithDiagnostic(diagnostic, "CLASS descriptor is malformed");
    }
    while (cursor < size && file.GetBase()[static_cast<size_t>(cursor)] != 0U) {
        if (cancellation.IsCancellationRequested()) {
            return false;
        }
        ++cursor;
    }
    if (!Advance(1U + sizeof(uint32_t), size, &cursor) || !ReadUleb32(file.GetBase(), size, &cursor, &ignored) ||
        !ReadUleb32(file.GetBase(), size, &cursor, &fieldCount) ||
        !ReadUleb32(file.GetBase(), size, &cursor, &methodCount) ||
        !SkipClassTags(file.GetBase(), size, &cursor, cancellation, diagnostic)) {
        return FailWithDiagnostic(diagnostic, "CLASS header is truncated or malformed");
    }
    for (uint32_t index = 0U; index < fieldCount; ++index) {
        if (cancellation.IsCancellationRequested() ||
            !SkipField(file.GetBase(), size, &cursor, cancellation, diagnostic)) {
            return false;
        }
    }
    for (uint32_t index = 0U; index < methodCount; ++index) {
        if (cancellation.IsCancellationRequested()) {
            return false;
        }
        MethodMetadata method;
        if (!ReadMethod(file, &cursor, &method, cancellation, diagnostic)) {
            return false;
        }
        if (!methods->emplace(method.methodOffset, method).second) {
            return FailWithDiagnostic(diagnostic, "METHOD offset is duplicated");
        }
    }
    return true;
}

}  // namespace

MethodIndexResult MethodIndex::Build(const panda_file::File &file, const CancellationToken &cancellation)
{
    if (cancellation.IsCancellationRequested()) {
        return {MethodIndexStatus::CANCELLED, nullptr, "METHOD index build was cancelled"};
    }
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (fileSize > std::numeric_limits<size_t>::max()) {
        return {MethodIndexStatus::MALFORMED_FILE, nullptr, "ABC file cannot be addressed on this host"};
    }

    std::unordered_map<uint64_t, MethodMetadata> methods;
    std::string diagnostic;
    for (const uint32_t classOffset : file.GetClasses()) {
        if (cancellation.IsCancellationRequested()) {
            return {MethodIndexStatus::CANCELLED, nullptr, "METHOD index build was cancelled"};
        }
        const panda_file::File::EntityId classId(classOffset);
        if (!classId.IsValid() || classOffset >= fileSize) {
            return {MethodIndexStatus::MALFORMED_FILE, nullptr, "Class table contains an invalid offset"};
        }
        if (!file.IsExternal(classId) && !ReadClass(file, classOffset, cancellation, &methods, &diagnostic)) {
            if (cancellation.IsCancellationRequested()) {
                return {MethodIndexStatus::CANCELLED, nullptr, "METHOD index build was cancelled"};
            }
            return {MethodIndexStatus::MALFORMED_FILE, nullptr,
                    diagnostic.empty() ? "ABC METHOD layout is malformed" : std::move(diagnostic)};
        }
    }
    std::vector<MethodMetadata> ordered;
    ordered.reserve(methods.size());
    for (const auto &[offset, method] : methods) {
        static_cast<void>(offset);
        ordered.push_back(method);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &left, const auto &right) { return left.methodOffset < right.methodOffset; });
    auto index = std::make_shared<const MethodIndex>(ConstructorToken {}, std::move(methods), std::move(ordered));
    return {MethodIndexStatus::SUCCESS, std::move(index), {}};
}

}  // namespace panda::bytecode_explorer
