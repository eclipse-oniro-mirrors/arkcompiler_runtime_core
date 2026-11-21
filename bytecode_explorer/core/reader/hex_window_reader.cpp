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

#include "core/reader/hex_window_reader.h"

#include "libpandafile/file.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace panda::bytecode_explorer {

Result<HexWindow> HexWindowReader::Read(const AbcDocument &document, uint64_t offset, uint64_t requestedSize)
{
    if (requestedSize == 0U || requestedSize > MAX_WINDOW_SIZE) {
        return Result<HexWindow>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Hex window size must be between 1 and 65536 bytes"));
    }
    const uint64_t fileSize = document.Summary().fileSize;
    if (offset > fileSize) {
        return Result<HexWindow>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Hex window offset is outside the ABC file"));
    }
    const uint64_t actualSize = std::min(requestedSize, fileSize - offset);
    HexWindow window;
    window.range = FileRange {offset, actualSize};
    window.eof = actualSize == fileSize - offset;
    if (actualSize != 0U) {
        const auto *begin = document.File().GetBase() + static_cast<size_t>(offset);  // NOLINT
        window.bytes.assign(begin, begin + static_cast<size_t>(actualSize));
    }
    return Result<HexWindow>::Success(std::move(window));
}

}  // namespace panda::bytecode_explorer
