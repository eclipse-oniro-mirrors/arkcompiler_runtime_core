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

#ifndef BYTECODE_EXPLORER_CORE_DOCUMENT_ABC_DOCUMENT_H
#define BYTECODE_EXPLORER_CORE_DOCUMENT_ABC_DOCUMENT_H

#include "core/common/result.h"
#include "core/reader/debug_build_detector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

// Size of the ABC format version field as stored in the file header.
constexpr size_t ABC_VERSION_SIZE = 4U;

struct DocumentSummary {
    std::string path;
    uint64_t fileSize {0};
    uint32_t checksum {0};
    std::array<uint8_t, ABC_VERSION_SIZE> version {};
    BuildModeReport build;
};

class AbcDocument final {
public:
    using OpenResult = Result<std::shared_ptr<const AbcDocument>>;

    static OpenResult Open(const std::string &path);

    ~AbcDocument();

    AbcDocument(const AbcDocument &) = delete;
    AbcDocument &operator=(const AbcDocument &) = delete;
    AbcDocument(AbcDocument &&) = delete;
    AbcDocument &operator=(AbcDocument &&) = delete;

    const DocumentSummary &Summary() const
    {
        return summary_;
    }

    const panda_file::File &File() const
    {
        return *file_;
    }

    // Passkey: only Open() can mint a token, which keeps the constructor usable
    // with std::make_shared while staying unreachable for any external caller.
    class ConstructorToken {
        friend class AbcDocument;

    private:
        ConstructorToken() = default;
    };

    AbcDocument(ConstructorToken, DocumentSummary summary, std::unique_ptr<const panda_file::File> file);

private:
    DocumentSummary summary_;
    std::unique_ptr<const panda_file::File> file_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_DOCUMENT_ABC_DOCUMENT_H
