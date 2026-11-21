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

#ifndef BYTECODE_EXPLORER_CORE_METADATA_METADATA_MODEL_H
#define BYTECODE_EXPLORER_CORE_METADATA_METADATA_MODEL_H

#include "core/common/result.h"
#include "core/model/entity.h"
#include "core/model/file_range.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

enum class MetadataKind : uint8_t {
    STRING,
    METHOD,
    LITERAL_ARRAY,
};

enum class MetadataOrigin : uint8_t {
    INDEX,
    STRUCTURE,
    LITERAL,
    INSTRUCTION,
};

enum class MetadataReferenceKind : uint8_t {
    RECORD_NAME,
    METHOD_NAME,
    FIELD_NAME,
    ANNOTATION_NAME,
    ANNOTATION_ELEMENT_NAME,
    ANNOTATION_STRING_VALUE,
    MODULE_REQUEST,
    IMPORT_NAME,
    EXPORT_NAME,
    LOCAL_NAME,
    SCOPE_NAME,
    LITERAL_STRING,
    LITERAL_METHOD,
    NESTED_LITERAL_ARRAY,
    INSTRUCTION_OPERAND,
};

struct MetadataObjectKey {
    MetadataKind kind {MetadataKind::STRING};
    uint64_t offset {0U};

    bool operator==(const MetadataObjectKey &other) const
    {
        return kind == other.kind && offset == other.offset;
    }

    bool operator<(const MetadataObjectKey &other) const
    {
        return std::tie(kind, offset) < std::tie(other.kind, other.offset);
    }
};

struct MetadataObject {
    MetadataObjectKey key;
    std::optional<FileRange> range;
    std::string preview;
    bool truncated {false};
    std::vector<MetadataOrigin> origins;
    std::vector<EntityId> entityIds;
    std::vector<std::string> diagnostics;
    size_t referenceCount {0U};
};

namespace metadata_model_detail {

template <class T>
inline void AppendMissing(std::vector<T> *target, const std::vector<T> &source)
{
    for (const auto &value : source) {
        if (std::find(target->begin(), target->end(), value) == target->end()) {
            target->push_back(value);
        }
    }
    std::sort(target->begin(), target->end());
}

}  // namespace metadata_model_detail

inline bool ShouldReplaceMetadataObjectPreview(const MetadataObject &current, const MetadataObject &candidate)
{
    return current.preview.empty() || (current.truncated && !candidate.truncated);
}

void MergeMetadataObjectEvidence(MetadataObject *target, const MetadataObject &source);

struct MetadataObjectDetail {
    MetadataObject object;
    std::optional<std::string> value;
    bool valueTruncated {false};
};

struct MetadataLimits {
    static constexpr uint64_t DEFAULT_MAX_PERSISTENT_BYTES = 128U * 1024U * 1024U;
    static constexpr size_t MAX_PREVIEW_BYTES = 512U;
    static constexpr size_t MAX_DETAIL_BYTES = 64U * 1024U;
    uint64_t maxPersistentBytes {DEFAULT_MAX_PERSISTENT_BYTES};
};

namespace metadata_model_detail {

inline bool IsUtf8Continuation(uint8_t value)
{
    return (value & 0xC0U) == 0x80U;
}

size_t Utf8SequenceLength(uint8_t first);
bool IsValidUtf8Sequence(const uint8_t *bytes, size_t length);
void AppendBoundedUtf8(std::string *target, std::string_view source, size_t limit);
void AppendBoundedUtf8(std::string *target, const char *source, size_t limit);

}  // namespace metadata_model_detail

inline std::string MakeBoundedMetadataDiagnostic(std::string_view prefix, const char *detail = nullptr)
{
    std::string diagnostic;
    diagnostic.reserve(MetadataLimits::MAX_PREVIEW_BYTES);
    metadata_model_detail::AppendBoundedUtf8(&diagnostic, prefix, MetadataLimits::MAX_PREVIEW_BYTES);
    metadata_model_detail::AppendBoundedUtf8(&diagnostic, detail, MetadataLimits::MAX_PREVIEW_BYTES);
    return diagnostic;
}

inline std::string MakeBoundedMetadataDiagnostic(std::string_view prefix, std::string_view detail)
{
    std::string diagnostic;
    diagnostic.reserve(MetadataLimits::MAX_PREVIEW_BYTES);
    metadata_model_detail::AppendBoundedUtf8(&diagnostic, prefix, MetadataLimits::MAX_PREVIEW_BYTES);
    metadata_model_detail::AppendBoundedUtf8(&diagnostic, detail, MetadataLimits::MAX_PREVIEW_BYTES);
    return diagnostic;
}

enum class MetadataIndexBuildStatus : uint8_t {
    READY,
    PARTIAL,
    CANCELLED,
    FAILED,
};

enum class MetadataIndexPhase : uint8_t {
    NOT_STARTED,
    BUILDING,
    READY,
    PARTIAL,
    FAILED,
    CANCELLED,
};

struct MetadataIndexLimits {
    static constexpr uint64_t DEFAULT_MAX_PERSISTENT_BYTES = MetadataLimits::DEFAULT_MAX_PERSISTENT_BYTES;
    static constexpr size_t DEFAULT_BATCH_METHODS = 32U;
    static constexpr std::chrono::milliseconds DEFAULT_BATCH_INTERVAL {200};

    uint64_t maxPersistentBytes {DEFAULT_MAX_PERSISTENT_BYTES};
    size_t batchMethods {DEFAULT_BATCH_METHODS};
    std::chrono::milliseconds batchInterval {DEFAULT_BATCH_INTERVAL};
};

struct MetadataStringRead {
    FileRange range;
    std::string text;
    bool truncated {false};
};

Result<MetadataStringRead> ReadMetadataString(const panda_file::File &file, uint64_t offset, size_t maxTextBytes);

struct MetadataReference {
    MetadataObjectKey target;
    MetadataReferenceKind kind {MetadataReferenceKind::RECORD_NAME};
    std::vector<EntityId> sourceEntityIds;
    std::optional<uint64_t> sourceMethodOffset;
    std::optional<uint32_t> pc;
    std::optional<uint32_t> operandIndex;
    std::optional<FileRange> sourceRange;
    std::string sourceName;
    std::string mnemonic;
};

struct MetadataIndexBatch {
    size_t processedMethods {0U};
    std::vector<MetadataObject> objects;
    std::vector<MetadataReference> references;
    std::vector<std::string> diagnostics;
};

struct MetadataIndexBuildResult {
    MetadataIndexBuildStatus status {MetadataIndexBuildStatus::FAILED};
    size_t scannedMethods {0U};
    std::vector<std::string> diagnostics;
};

struct MetadataIndexStatus {
    MetadataIndexPhase phase {MetadataIndexPhase::NOT_STARTED};
    uint64_t revision {0U};
    size_t scannedMethods {0U};
    size_t totalMethods {0U};
    size_t objectCount {0U};
    size_t referenceCount {0U};
    std::vector<std::string> diagnostics;
    bool diagnosticsTruncated {false};
};

struct MetadataObjectQuery {
    std::optional<MetadataKind> kind;
    std::string query;
    std::optional<std::string> cursor;
    size_t limit {100U};
};

template <class T>
struct MetadataPage {
    uint64_t revision {0U};
    size_t total {0U};
    std::vector<T> items;
    std::optional<std::string> nextCursor;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_METADATA_METADATA_MODEL_H
