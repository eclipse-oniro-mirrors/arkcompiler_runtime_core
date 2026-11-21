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

#include "core/metadata/metadata_model.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {

void MergeMetadataObjectEvidence(MetadataObject *target, const MetadataObject &source)
{
    if (!target->range.has_value()) {
        target->range = source.range;
    }
    if (ShouldReplaceMetadataObjectPreview(*target, source)) {
        target->preview = source.preview;
        target->truncated = source.truncated;
    }
    metadata_model_detail::AppendMissing(&target->origins, source.origins);
    metadata_model_detail::AppendMissing(&target->entityIds, source.entityIds);
    metadata_model_detail::AppendMissing(&target->diagnostics, source.diagnostics);
    target->referenceCount += source.referenceCount;
}

namespace metadata_model_detail {

size_t Utf8SequenceLength(uint8_t first)
{
    if (first <= 0x7FU) {
        return 1U;
    }
    if (first >= 0xC2U && first <= 0xDFU) {
        return 2U;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
        return 3U;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
        return 4U;
    }
    return 0U;
}

bool IsValidUtf8Sequence(const uint8_t *bytes, size_t length)
{
    if ((length > 1U && !IsUtf8Continuation(bytes[1U])) || (length > 2U && !IsUtf8Continuation(bytes[2U])) ||
        (length > 3U && !IsUtf8Continuation(bytes[3U]))) {
        return false;
    }
    const auto first = bytes[0U];
    const auto second = length > 1U ? bytes[1U] : 0U;
    return !((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU) ||
             (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU));
}

void AppendBoundedUtf8(std::string *target, std::string_view source, size_t limit)
{
    size_t offset = 0U;
    while (offset < source.size() && target->size() < limit) {
        const auto first = static_cast<uint8_t>(source[offset]);
        const auto length = Utf8SequenceLength(first);
        if (length == 0U || length > source.size() - offset || length > limit - target->size()) {
            return;
        }
        uint8_t bytes[4U] {first, 0U, 0U, 0U};
        for (size_t index = 1U; index < length; ++index) {
            bytes[index] = static_cast<uint8_t>(source[offset + index]);
        }
        if (!IsValidUtf8Sequence(bytes, length)) {
            return;
        }
        target->append(source.data() + offset, length);
        offset += length;
    }
}

void AppendBoundedUtf8(std::string *target, const char *source, size_t limit)
{
    if (source == nullptr) {
        return;
    }
    while (target->size() < limit) {
        const auto first = static_cast<uint8_t>(*source);
        if (first == 0U) {
            return;
        }
        const auto length = Utf8SequenceLength(first);
        if (length == 0U || length > limit - target->size()) {
            return;
        }
        uint8_t bytes[4U] {first, 0U, 0U, 0U};
        for (size_t index = 1U; index < length; ++index) {
            bytes[index] = static_cast<uint8_t>(source[index]);
            if (bytes[index] == 0U) {
                return;
            }
        }
        if (!IsValidUtf8Sequence(bytes, length)) {
            return;
        }
        target->append(source, length);
        source += length;
    }
}

}  // namespace metadata_model_detail
}  // namespace panda::bytecode_explorer
