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

#ifndef BYTECODE_EXPLORER_CORE_INDEX_FILE_RANGE_LOCATOR_H
#define BYTECODE_EXPLORER_CORE_INDEX_FILE_RANGE_LOCATOR_H

#include "core/index/structure_index.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace panda::bytecode_explorer {

enum class FileRangeSource : uint8_t {
    PRIMARY,
    BACKING,
};

struct FileRangeMatch {
    EntityId entityId {INVALID_ENTITY_ID};
    FileRangeSource source {FileRangeSource::PRIMARY};
    FileRange range;
    std::optional<size_t> backingIndex;
};

class FileRangeLocator final {
public:
    static std::shared_ptr<const FileRangeLocator> Build(const StructureIndex &structure, uint64_t fileSize);

    std::optional<FileRangeMatch> FindMostSpecific(uint64_t offset) const;

private:
    struct Segment {
        uint64_t begin {0U};
        uint64_t end {0U};
        FileRangeMatch match;
    };

public:
    // Passkey: only Build() can mint a token, which keeps the constructor usable
    // with std::make_shared while staying unreachable for any external caller.
    class ConstructorToken {
        friend class FileRangeLocator;

    private:
        ConstructorToken() = default;
    };

    FileRangeLocator(ConstructorToken, std::vector<Segment> segments);

private:
    std::vector<Segment> segments_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_INDEX_FILE_RANGE_LOCATOR_H
