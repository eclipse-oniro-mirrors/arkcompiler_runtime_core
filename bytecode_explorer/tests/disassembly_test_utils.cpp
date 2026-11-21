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

#include "tests/disassembly_test_utils.h"

namespace panda::bytecode_explorer::test {

CodeLayout GetCodeLayout(const panda_file::File &file, const ExecutableTarget &target)
{
    panda_file::MethodDataAccessor method(file, panda_file::File::EntityId(static_cast<uint32_t>(target.methodOffset)));
    const auto codeId = method.GetCodeId();
    EXPECT_TRUE(codeId.has_value());
    if (!codeId.has_value()) {
        return {};
    }

    panda_file::CodeDataAccessor code(file, *codeId);
    uint64_t cursor = codeId->GetOffset();
    cursor = UlebEndOffset(file, cursor);  // num_vregs
    cursor = UlebEndOffset(file, cursor);  // num_args
    const auto codeSizeFieldOffset = cursor;
    cursor = UlebEndOffset(file, cursor);  // code_size
    const auto triesSizeFieldOffset = cursor;
    cursor = UlebEndOffset(file, cursor);  // tries_size
    const auto instructionsOffset = file.GetIdFromPointer(code.GetInstructions()).GetOffset();
    EXPECT_EQ(cursor, instructionsOffset);
    EXPECT_LE(code.GetCodeSize(), file.GetHeader()->file_size - instructionsOffset);
    return {codeId->GetOffset(),
            codeSizeFieldOffset,
            triesSizeFieldOffset,
            instructionsOffset,
            instructionsOffset + code.GetCodeSize(),
            code.GetCodeSize()};
}

}  // namespace panda::bytecode_explorer::test
