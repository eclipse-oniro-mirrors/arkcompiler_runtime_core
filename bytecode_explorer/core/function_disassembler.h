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

#ifndef BYTECODE_EXPLORER_CORE_FUNCTION_DISASSEMBLER_H
#define BYTECODE_EXPLORER_CORE_FUNCTION_DISASSEMBLER_H

#include "core/common/cancellation.h"
#include "core/disassembly.h"
#include "core/method_index.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

enum class DisassemblyStatus {
    SUCCESS,
    INVALID_METHOD,
    NO_CODE,
    MALFORMED_BYTECODE,
    RESOURCE_LIMIT_EXCEEDED,
    CANCELLED,
};

// Defaults are centralized here so production decoding remains responsive and
// tests can inject small thresholds without allocating oversized fixtures.
struct DisassemblyLimits {
    static constexpr uint64_t DEFAULT_MAX_CODE_BYTES = 4U * 1024U * 1024U;
    static constexpr uint64_t DEFAULT_MAX_INSTRUCTION_COUNT = 500000U;
    static constexpr uint64_t DEFAULT_MAX_TRY_BLOCK_COUNT = 65536U;
    static constexpr uint64_t DEFAULT_MAX_TOTAL_CATCH_COUNT = 262144U;
    static constexpr uint64_t DEFAULT_MAX_FORMATTED_TEXT_BYTES = 32U * 1024U * 1024U;
    static constexpr uint64_t DEFAULT_MAX_MODEL_BYTES = 128U * 1024U * 1024U;

    uint64_t maxCodeBytes {DEFAULT_MAX_CODE_BYTES};
    uint64_t maxInstructionCount {DEFAULT_MAX_INSTRUCTION_COUNT};
    uint64_t maxTryBlockCount {DEFAULT_MAX_TRY_BLOCK_COUNT};
    uint64_t maxTotalCatchCount {DEFAULT_MAX_TOTAL_CATCH_COUNT};
    uint64_t maxFormattedTextBytes {DEFAULT_MAX_FORMATTED_TEXT_BYTES};
    uint64_t maxModelBytes {DEFAULT_MAX_MODEL_BYTES};
};

struct DisassemblyResult {
    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    FunctionDisassemblyPtr disassembly;
    std::string diagnostic;
};

class FunctionDisassembler final {
public:
    DisassemblyResult Disassemble(const panda_file::File &file, const MethodIndex &methods,
                                  const ExecutableTarget &target, const DisassemblyLimits &limits = {},
                                  const CancellationToken &cancellation = {}) const;
    DisassemblyResult Disassemble(const panda_file::File &file, const ExecutableTarget &target,
                                  const DisassemblyLimits &limits = {},
                                  const CancellationToken &cancellation = {}) const;

    static uint64_t EstimateModelBytes(const FunctionDisassembly &disassembly);
    static uint64_t EstimatePeakModelBytes(const FunctionDisassembly &disassembly);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_FUNCTION_DISASSEMBLER_H
