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

#ifndef BYTECODE_EXPLORER_CORE_READER_HEX_WINDOW_READER_H
#define BYTECODE_EXPLORER_CORE_READER_HEX_WINDOW_READER_H

#include "core/common/result.h"
#include "core/document/abc_document.h"
#include "core/model/file_range.h"

#include <cstdint>
#include <vector>

namespace panda::bytecode_explorer {

struct HexWindow {
    FileRange range;
    std::vector<uint8_t> bytes;
    bool eof {false};
};

class HexWindowReader final {
public:
    static constexpr uint64_t DEFAULT_WINDOW_SIZE = 16U * 1024U;
    static constexpr uint64_t MAX_WINDOW_SIZE = 64U * 1024U;

    static Result<HexWindow> Read(const AbcDocument &document, uint64_t offset, uint64_t requestedSize);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_HEX_WINDOW_READER_H
