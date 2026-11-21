/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef ASSEMBLER_ASSEMBLY_DEBUG_H
#define ASSEMBLER_ASSEMBLY_DEBUG_H

#include <cstdint>
#include <string>
#include <string_view>
#include <cstdint>

#include "assembly-string-pool.h"

namespace panda::pandasm::debuginfo {

struct Ins {
    uint32_t line_number = 0;
    uint32_t column_number = 0;

    void SetLineNumber(uint32_t ln)
    {
        line_number = ln;
    }

    void SetColumnNumber(size_t cn)
    {
        column_number = cn;
    }

    Ins() = default;
    explicit Ins(uint32_t l_n)
        : line_number(l_n)
    {
    }
};

// The variable name is interned in the process-wide string pool: local variable
// names repeat heavily across functions, so every entry stores only a stable
// pointer to the shared copy instead of its own std::string.
struct LocalVariable {
    const std::string *name = nullptr;
    int32_t reg = 0;
    uint32_t start = 0;
    uint32_t length = 0;

    const std::string &Name() const
    {
        static const std::string EMPTY;
        return name != nullptr ? *name : EMPTY;
    }

    void SetName(std::string_view n)
    {
        name = InternString(n);
    }
};

}  // namespace panda::pandasm::debuginfo

#endif  // ASSEMBLER_ASSEMBLY_DEBUG_H
