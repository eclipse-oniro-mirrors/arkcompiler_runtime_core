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

#ifndef BYTECODE_EXPLORER_CORE_METHOD_INDEX_H
#define BYTECODE_EXPLORER_CORE_METHOD_INDEX_H

#include "core/common/cancellation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

struct MethodMetadata {
    uint64_t methodOffset {0U};
    std::optional<uint32_t> codeOffset;
};

enum class MethodIndexStatus {
    SUCCESS,
    MALFORMED_FILE,
    CANCELLED,
};

class MethodIndex;

struct MethodIndexResult {
    MethodIndexStatus status {MethodIndexStatus::MALFORMED_FILE};
    std::shared_ptr<const MethodIndex> index;
    std::string diagnostic;
};

class MethodIndex final {
public:
    static MethodIndexResult Build(const panda_file::File &file, const CancellationToken &cancellation = {});

    const MethodMetadata *Find(uint64_t methodOffset) const
    {
        const auto found = methods_.find(methodOffset);
        return found == methods_.end() ? nullptr : &found->second;
    }

    size_t Size() const
    {
        return methods_.size();
    }

    const std::vector<MethodMetadata> &MethodsInOffsetOrder() const
    {
        return ordered_;
    }

    // Passkey: only Build() can mint a token, which keeps the constructor usable
    // with std::make_shared while staying unreachable for any external caller.
    class ConstructorToken {
        friend class MethodIndex;

    private:
        ConstructorToken() = default;
    };

    MethodIndex(ConstructorToken, std::unordered_map<uint64_t, MethodMetadata> methods,
                std::vector<MethodMetadata> ordered)
        : methods_(std::move(methods)), ordered_(std::move(ordered))
    {
    }

private:
    std::unordered_map<uint64_t, MethodMetadata> methods_;
    std::vector<MethodMetadata> ordered_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_METHOD_INDEX_H
