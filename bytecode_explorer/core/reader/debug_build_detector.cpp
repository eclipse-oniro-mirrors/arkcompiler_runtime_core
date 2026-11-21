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

#include "core/reader/debug_build_detector.h"

#include "libpandafile/bytecode_instruction-inl.h"
#include "libpandafile/class_data_accessor-inl.h"
#include "libpandafile/code_data_accessor-inl.h"
#include "libpandafile/debug_data_accessor-inl.h"
#include "libpandafile/file-inl.h"
#include "libpandafile/file_items.h"
#include "libpandafile/helpers.h"
#include "libpandafile/method_data_accessor-inl.h"
#include "utils/span.h"

#include <cstddef>
#include <exception>

namespace panda::bytecode_explorer {
namespace {

// Guards against pathological or corrupt line programs: a real program's
// opcode count is bounded by its instruction count and stays far below this.
constexpr uint64_t MAX_LINE_PROGRAM_OPCODES = 1000000ULL;

using BytecodeOpcode = BytecodeInstruction::Opcode;
using LineOpcode = panda_file::LineNumberProgramItem::Opcode;

bool IsDebugOnlyInstruction(BytecodeOpcode opcode)
{
    // es2panda emits these exclusively from its IsDebug() paths: debugger
    // statements survive compilation only in debug builds, and lexical
    // environments carry their variable-name literal array only in debug.
    switch (opcode) {
        case BytecodeOpcode::DEBUGGER:
        case BytecodeOpcode::NEWLEXENVWITHNAME_IMM8_ID16:
        case BytecodeOpcode::WIDE_NEWLEXENVWITHNAME_PREF_IMM16_ID16:
            return true;
        default:
            return false;
    }
}

size_t ValidatedInstructionSize(const BytecodeInstruction &instruction, uint32_t remainingSize)
{
    if (!instruction.IsPrimaryOpcodeValid()) {
        throw panda_file::helpers::FileAccessException("Invalid bytecode primary opcode");
    }
    if (instruction.IsPrefixed() && remainingSize < 2U) {
        throw panda_file::helpers::FileAccessException("Truncated prefixed bytecode opcode");
    }
    if (!instruction.IsOpcodeValid()) {
        throw panda_file::helpers::FileAccessException("Invalid bytecode opcode");
    }
    const auto instructionSize = instruction.GetSize();
    if (instructionSize == 0U || instructionSize > remainingSize) {
        throw panda_file::helpers::FileAccessException("Truncated bytecode instruction");
    }
    return instructionSize;
}

void ScanMethodCode(const panda_file::CodeDataAccessor &code, BuildModeEvidence *evidence)
{
    const uint8_t *instructions = code.GetInstructions();
    uint32_t pc = 0U;
    while (pc < code.GetCodeSize()) {
        BytecodeInstruction instruction(instructions + pc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const auto instructionSize = ValidatedInstructionSize(instruction, code.GetCodeSize() - pc);
        if (IsDebugOnlyInstruction(instruction.GetOpcode())) {
            ++evidence->debugOnlyInstructions;
        }
        pc += static_cast<uint32_t>(instructionSize);
    }
}

// Walks one line number program, counting the opcodes that only a debug
// compilation emits. The upstream LineNumberProgramProcessor is not reused on
// purpose: it aborts the process on RESTART_LOCAL and stops early on
// SET_PROLOGUE_END / SET_EPILOGUE_BEGIN, and neither can be tolerated when
// scanning arbitrary local files. Operand decoding follows the same encoding:
// register numbers are inline sleb128 in the program stream, while the other
// operands are uleb128/sleb128 values read from the per-method constant pool.
class LineProgramScanner {
public:
    LineProgramScanner(const panda_file::File &file, const uint8_t *program, Span<const uint8_t> constantPool,
                       BuildModeEvidence *evidence)
        : fileEnd_(file.GetBase() + file.GetHeader()->file_size),
          program_(program),
          constantPool_(constantPool),
          evidence_(evidence)
    {
    }

    void Scan()
    {
        auto opcode = ReadNextOpcode();
        while (opcode != LineOpcode::END_SEQUENCE) {
            ApplyOpcode(opcode);
            if (--opcodeBudget_ == 0U) {
                throw panda_file::helpers::FileAccessException("Line number program opcode budget exceeded");
            }
            opcode = ReadNextOpcode();
        }
    }

private:
    LineOpcode ReadNextOpcode()
    {
        if (!HasProgramByte()) {
            throw panda_file::helpers::FileAccessException("Line number program crosses the file boundary");
        }
        return static_cast<LineOpcode>(*program_++);
    }

    // Applies one non-terminating line-number opcode; special opcodes only advance the
    // program implicitly through their encoding.
    void ApplyOpcode(LineOpcode opcode)
    {
        switch (opcode) {
            case LineOpcode::ADVANCE_PC:
            case LineOpcode::SET_FILE:
            case LineOpcode::SET_SOURCE_CODE:
            case LineOpcode::SET_COLUMN:
                ReadPoolUleb128();
                break;
            case LineOpcode::ADVANCE_LINE:
                ReadPoolSleb128();
                break;
            case LineOpcode::START_LOCAL:
                ReadInlineSleb128();
                ReadPoolUleb128();
                ReadPoolUleb128();
                ++evidence_->variableTableEntries;
                break;
            case LineOpcode::START_LOCAL_EXTENDED:
                ReadInlineSleb128();
                ReadPoolUleb128();
                ReadPoolUleb128();
                ReadPoolUleb128();
                ++evidence_->variableTableEntries;
                break;
            case LineOpcode::END_LOCAL:
            case LineOpcode::RESTART_LOCAL:
                ReadInlineSleb128();
                break;
            case LineOpcode::SET_PROLOGUE_END:
            case LineOpcode::SET_EPILOGUE_BEGIN:
                break;
            default:
                // Special opcodes encode line and pc deltas in their value.
                break;
        }
    }

    bool HasProgramByte() const
    {
        return program_ < fileEnd_;
    }

    void ReadPoolUleb128()
    {
        (void)panda_file::helpers::ReadULeb128(&constantPool_);
    }

    void ReadPoolSleb128()
    {
        (void)panda_file::helpers::ReadLeb128(&constantPool_);
    }

    void ReadInlineSleb128()
    {
        if (!HasProgramByte()) {
            throw panda_file::helpers::FileAccessException("Line number program crosses the file boundary");
        }
        const auto [value, size, isFull] = leb128::DecodeSigned<int32_t>(program_);
        if (!isFull || size == 0U) {
            throw panda_file::helpers::FileAccessException("Malformed register number in line number program");
        }
        if (static_cast<size_t>(fileEnd_ - program_) < size) {
            throw panda_file::helpers::FileAccessException("Line number program crosses the file boundary");
        }
        program_ += size;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        (void)value;
    }

    const uint8_t *fileEnd_;
    const uint8_t *program_;
    Span<const uint8_t> constantPool_;
    BuildModeEvidence *evidence_;
    uint64_t opcodeBudget_ {MAX_LINE_PROGRAM_OPCODES};
};

void ScanMethodDebugInfo(const panda_file::File &file, panda_file::DebugInfoDataAccessor *debugInfo,
                         BuildModeEvidence *evidence)
{
    LineProgramScanner scanner(file, debugInfo->GetLineNumberProgram(), debugInfo->GetConstantPool(), evidence);
    scanner.Scan();
}

// Scans one METHOD's code item and debug-info sections, accumulating evidence.
void ScanMethod(const panda_file::File &file, panda_file::MethodDataAccessor &method, BuildModeReport *report)
{
    ++report->evidence.scannedMethods;
    const auto codeId = method.GetCodeId();
    if (codeId.has_value()) {
        panda_file::CodeDataAccessor code(file, *codeId);
        ScanMethodCode(code, &report->evidence);
    }
    const auto debugInfoId = method.GetDebugInfoId();
    if (debugInfoId.has_value()) {
        panda_file::DebugInfoDataAccessor debugInfo(file, *debugInfoId);
        ScanMethodDebugInfo(file, &debugInfo, &report->evidence);
    }
}

}  // namespace

BuildModeReport DetectAbcBuildMode(const panda_file::File &file)
{
    BuildModeReport report;
    try {
        for (const uint32_t offset : file.GetClasses()) {
            const panda_file::File::EntityId recordId(offset);
            if (file.IsExternal(recordId)) {
                continue;
            }
            panda_file::ClassDataAccessor record(file, recordId);
            record.EnumerateMethods(
                [&file, &report](panda_file::MethodDataAccessor &method) { ScanMethod(file, method, &report); });
        }
        report.evidence.scanComplete = true;
        report.mode = (report.evidence.debugOnlyInstructions > 0U || report.evidence.variableTableEntries > 0U ||
                       report.evidence.sourceCodeMarkers > 0U)
                          ? AbcBuildMode::DEBUG
                          : AbcBuildMode::RELEASE;
    } catch (const std::exception &) {
        report.mode = AbcBuildMode::UNKNOWN;
        report.evidence.scanComplete = false;
    }
    return report;
}

}  // namespace panda::bytecode_explorer
