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

#ifndef BYTECODE_EXPLORER_CORE_READER_LITERAL_ARRAY_READER_H
#define BYTECODE_EXPLORER_CORE_READER_LITERAL_ARRAY_READER_H

#include "core/model/physical_program.h"

#include <cstdint>
#include <set>
#include <vector>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

class LiteralArrayReader final {
public:
    static std::vector<PhysicalLiteralArray> Read(const panda_file::File &file, std::set<uint32_t> inventory,
                                                  std::set<uint32_t> traversable,
                                                  const std::set<uint32_t> &excludedOffsets);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_LITERAL_ARRAY_READER_H
