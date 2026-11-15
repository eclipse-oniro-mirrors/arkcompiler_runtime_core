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

#ifndef BYTECODE_EXPLORER_CORE_FORMAT_DISASSEMBLY_FORMATTER_H
#define BYTECODE_EXPLORER_CORE_FORMAT_DISASSEMBLY_FORMATTER_H

#include "core/disassembly.h"

#include <cstdint>
#include <optional>
#include <string>

namespace panda::bytecode_explorer {

class DisassemblyFormatter final {
public:
    static std::string FormatPc(uint32_t pc);
    static std::string FormatOpcode(uint16_t opcode);
    static std::string FormatInstructionAbcOffset(uint64_t instructionsOffset, uint32_t pc);
    static std::string FormatFunctionHeader(const FunctionDisassembly &disassembly);
    static std::string FormatBytes(const ModelVector<uint8_t> &bytes);
    static std::string FormatRegister(uint16_t reg, uint32_t vregCount);
    static bool FormatRegister(uint16_t reg, uint32_t vregCount, uint64_t maxBytes, ModelString *formatted);
    static std::string FormatRegisters(const ModelVector<uint16_t> &registers);
    static std::string FormatRegisters(const ModelVector<uint16_t> &registers, uint32_t vregCount);
    static std::string FormatImmediateOperand(const ImmediateValue &immediate);
    static bool FormatImmediateOperand(const ImmediateValue &immediate, uint64_t maxBytes, ModelString *formatted);
    static bool FormatIdOperand(const IdOperand &id, uint64_t maxBytes, std::string *formatted);
    static bool FormatIdOperand(const IdOperand &id, uint64_t maxBytes, ModelString *formatted);
    static std::string FormatIdOperand(const IdOperand &id);
    static std::string FormatInstruction(const InstructionInfo &instruction);
    static std::string FormatJumpTarget(const std::optional<uint32_t> &target);
    static std::string FormatInstructionDetails(const InstructionInfo &instruction, uint32_t vregCount);
    static std::string FormatTooltip(const InstructionInfo &instruction);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_FORMAT_DISASSEMBLY_FORMATTER_H
