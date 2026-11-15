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

#include "core/format/disassembly_formatter.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {
namespace {

// Zero-padded hexadecimal field widths used by the fixed-width formatters.
constexpr int OFFSET_HEX_WIDTH = 8;
constexpr int OPCODE_HEX_WIDTH = 4;
constexpr int BYTE_HEX_WIDTH = 2;

void AppendFloatingValue(std::ostringstream *output, double value)
{
    std::array<char, 64U> buffer {};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec == std::errc {}) {
        output->write(buffer.data(), converted.ptr - buffer.data());
        return;
    }
    *output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

void AppendImmediate(std::ostringstream *output, const ImmediateValue &immediate)
{
    switch (immediate.kind) {
        case ImmediateKind::SIGNED:
            *output << 'i' << immediate.bitWidth << ' ' << std::get<int64_t>(immediate.value);
            break;
        case ImmediateKind::UNSIGNED:
            *output << 'u' << immediate.bitWidth << ' ' << std::get<uint64_t>(immediate.value);
            break;
        case ImmediateKind::FLOAT:
            *output << 'f' << immediate.bitWidth << ' ';
            AppendFloatingValue(output, std::get<double>(immediate.value));
            break;
    }
    const auto hexDigits = static_cast<int>((immediate.bitWidth + 3U) / 4U);
    *output << " (raw 0x" << std::uppercase << std::hex << std::setw(hexDigits) << std::setfill('0')
            << immediate.rawBits << std::dec << std::setfill(' ') << ')';
}

void AppendImmediates(std::ostringstream *output, const ModelVector<ImmediateValue> &values)
{
    if (values.empty()) {
        *output << "none";
        return;
    }
    for (size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) {
            *output << ", ";
        }
        AppendImmediate(output, values[index]);
    }
}

template <class String>
bool TryAppend(std::string_view text, uint64_t maxBytes, String *output)
{
    if (output->size() > maxBytes || text.size() > maxBytes - output->size() ||
        text.size() > output->max_size() - output->size()) {
        return false;
    }
    output->append(text);
    return true;
}

template <class Value, class String>
bool TryAppendNumber(Value value, uint64_t maxBytes, String *output)
{
    std::array<char, 64U> buffer {};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return converted.ec == std::errc {} &&
           TryAppend(std::string_view(buffer.data(), static_cast<size_t>(converted.ptr - buffer.data())), maxBytes,
                     output);
}

template <class String>
bool TryAppendPc(uint32_t pc, uint64_t maxBytes, String *output)
{
    constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    std::array<char, 10U> formatted {'0', 'x'};
    for (size_t index = 0U; index < sizeof(pc) * 2U; ++index) {
        const auto shift = static_cast<uint32_t>((sizeof(pc) * 2U - index - 1U) * 4U);
        formatted[index + 2U] = HEX_DIGITS[(pc >> shift) & 0x0fU];
    }
    return TryAppend(std::string_view(formatted.data(), formatted.size()), maxBytes, output);
}

template <class String>
bool AppendEscaped(std::string_view text, uint64_t maxBytes, String *output)
{
    constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    for (const char character : text) {
        const auto byte = static_cast<uint8_t>(character);
        std::string_view escaped;
        switch (byte) {
            case '\\':
                escaped = "\\\\";
                break;
            case '"':
                escaped = "\\\"";
                break;
            case '\n':
                escaped = "\\n";
                break;
            case '\r':
                escaped = "\\r";
                break;
            case '\t':
                escaped = "\\t";
                break;
            case '\b':
                escaped = "\\b";
                break;
            case '\f':
                escaped = "\\f";
                break;
            default:
                break;
        }
        if (!escaped.empty()) {
            if (!TryAppend(escaped, maxBytes, output)) {
                return false;
            }
            continue;
        }
        if (byte < 0x20U || byte == 0x7fU) {
            const std::array<char, 4U> hexEscape {'\\', 'x', HEX_DIGITS[byte >> 4U], HEX_DIGITS[byte & 0x0fU]};
            if (!TryAppend(std::string_view(hexEscape.data(), hexEscape.size()), maxBytes, output)) {
                return false;
            }
            continue;
        }
        if (!TryAppend(std::string_view(&character, 1U), maxBytes, output)) {
            return false;
        }
    }
    return true;
}

const char *IdKindName(IdOperandKind kind)
{
    switch (kind) {
        case IdOperandKind::STRING:
            return "string";
        case IdOperandKind::METHOD:
            return "method";
        case IdOperandKind::LITERAL_ARRAY:
            return "literal array";
        case IdOperandKind::UNKNOWN:
            return "unknown";
    }
    return "unknown";
}

const char *IdResolutionModeName(IdResolutionMode mode)
{
    switch (mode) {
        case IdResolutionMode::INDEX:
            return "index";
        case IdResolutionMode::DIRECT_OFFSET:
            return "direct offset";
    }
    return "unknown";
}

std::string FormatAbcOffset(uint64_t offset)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(OFFSET_HEX_WIDTH) << std::setfill('0') << offset;
    return output.str();
}

void AppendRegisters(std::ostringstream *output, const ModelVector<uint16_t> &registers, uint32_t vregCount)
{
    if (registers.empty()) {
        *output << "none";
        return;
    }
    for (size_t index = 0U; index < registers.size(); ++index) {
        if (index != 0U) {
            *output << ", ";
        }
        *output << DisassemblyFormatter::FormatRegister(registers[index], vregCount) << " (raw " << registers[index]
                << ')';
    }
}

void AppendDetailedIds(std::ostringstream *output, const ModelVector<IdOperand> &ids)
{
    if (ids.empty()) {
        *output << "none";
        return;
    }
    for (size_t index = 0U; index < ids.size(); ++index) {
        if (index != 0U) {
            *output << '\n';
        }
        const auto &id = ids[index];
        *output << (index == 0U ? "" : "  ") << IdKindName(id.kind) << " ID: raw " << id.rawValue << ", mode "
                << IdResolutionModeName(id.resolutionMode) << ", resolved ABC offset ";
        if (id.resolvedOffset.has_value()) {
            *output << FormatAbcOffset(*id.resolvedOffset);
        } else {
            *output << "unavailable";
        }
        *output << ", display ";
        if (id.kind != IdOperandKind::UNKNOWN && !id.resolvedOffset.has_value()) {
            *output << "unavailable";
        } else {
            *output << DisassemblyFormatter::FormatIdOperand(id);
        }
    }
}

void AppendIds(std::ostringstream *output, const ModelVector<IdOperand> &ids)
{
    if (ids.empty()) {
        *output << "none";
        return;
    }
    for (size_t index = 0U; index < ids.size(); ++index) {
        if (index != 0U) {
            *output << ", ";
        }
        const auto &id = ids[index];
        *output << IdKindName(id.kind) << " #" << id.rawValue;
        if (id.resolvedOffset.has_value()) {
            *output << " -> " << DisassemblyFormatter::FormatPc(static_cast<uint32_t>(*id.resolvedOffset));
        }
        if (!id.resolvedText.empty()) {
            *output << ' ' << DisassemblyFormatter::FormatIdOperand(id);
        }
    }
}

}  // namespace

std::string DisassemblyFormatter::FormatPc(uint32_t pc)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(OFFSET_HEX_WIDTH) << std::setfill('0') << pc;
    return output.str();
}

std::string DisassemblyFormatter::FormatOpcode(uint16_t opcode)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(OPCODE_HEX_WIDTH) << std::setfill('0') << opcode;
    return output.str();
}

std::string DisassemblyFormatter::FormatInstructionAbcOffset(uint64_t instructionsOffset, uint32_t pc)
{
    if (instructionsOffset > std::numeric_limits<uint64_t>::max() - pc) {
        return "overflow";
    }
    return FormatAbcOffset(instructionsOffset + pc);
}

std::string DisassemblyFormatter::FormatFunctionHeader(const FunctionDisassembly &disassembly)
{
    std::ostringstream output;
    output << "METHOD metadata offset: " << FormatAbcOffset(disassembly.methodOffset) << '\n';
    output << "CODE item offset: " << FormatAbcOffset(disassembly.codeOffset) << '\n';
    output << "Bytecode instruction ABC range: [" << FormatAbcOffset(disassembly.instructionsOffset) << ", ";
    if (disassembly.instructionsOffset > std::numeric_limits<uint64_t>::max() - disassembly.codeSize) {
        output << "overflow";
    } else {
        output << FormatAbcOffset(disassembly.instructionsOffset + disassembly.codeSize);
    }
    output << ") (" << disassembly.codeSize << " bytes)\n";
    output << "Local vregs: " << disassembly.vregCount << '\n';
    output << "ABC argument slots: " << disassembly.argumentCount;
    constexpr uint32_t implicitArgumentCount = 3U;
    if (disassembly.argumentCount < implicitArgumentCount) {
        output << " (anomalous: fewer than implicit 3: CallTarget, NewTarget, this)";
    } else {
        output << " (source " << disassembly.argumentCount - implicitArgumentCount
               << " + implicit 3: CallTarget, NewTarget, this)";
    }
    return output.str();
}

std::string DisassemblyFormatter::FormatBytes(const ModelVector<uint8_t> &bytes)
{
    std::ostringstream output;
    for (size_t index = 0U; index < bytes.size(); ++index) {
        if (index != 0U) {
            output << ' ';
        }
        output << std::uppercase << std::hex << std::setw(BYTE_HEX_WIDTH) << std::setfill('0')
               << static_cast<uint32_t>(bytes[index]);
    }
    return output.str();
}

std::string DisassemblyFormatter::FormatRegister(uint16_t reg, uint32_t vregCount)
{
    if (reg < vregCount) {
        return "v" + std::to_string(reg);
    }
    return "a" + std::to_string(static_cast<uint32_t>(reg) - vregCount);
}

bool DisassemblyFormatter::FormatRegister(uint16_t reg, uint32_t vregCount, uint64_t maxBytes, ModelString *formatted)
{
    if (formatted == nullptr) {
        return false;
    }
    formatted->clear();
    const bool isArgument = reg >= vregCount;
    const auto value = isArgument ? static_cast<uint32_t>(reg) - vregCount : reg;
    return TryAppend(isArgument ? "a" : "v", maxBytes, formatted) && TryAppendNumber(value, maxBytes, formatted);
}

std::string DisassemblyFormatter::FormatRegisters(const ModelVector<uint16_t> &registers)
{
    return FormatRegisters(registers, std::numeric_limits<uint32_t>::max());
}

std::string DisassemblyFormatter::FormatRegisters(const ModelVector<uint16_t> &registers, uint32_t vregCount)
{
    std::ostringstream output;
    for (size_t index = 0U; index < registers.size(); ++index) {
        if (index != 0U) {
            output << ", ";
        }
        output << FormatRegister(registers[index], vregCount);
    }
    return output.str();
}

std::string DisassemblyFormatter::FormatImmediateOperand(const ImmediateValue &immediate)
{
    std::ostringstream output;
    switch (immediate.kind) {
        case ImmediateKind::SIGNED:
            output << (std::get<int64_t>(immediate.value));
            break;
        case ImmediateKind::UNSIGNED:
            output << (std::get<uint64_t>(immediate.value));
            break;
        case ImmediateKind::FLOAT:
            AppendFloatingValue(&output, std::get<double>(immediate.value));
            break;
    }
    return output.str();
}

bool DisassemblyFormatter::FormatImmediateOperand(const ImmediateValue &immediate, uint64_t maxBytes,
                                                  ModelString *formatted)
{
    if (formatted == nullptr) {
        return false;
    }
    formatted->clear();
    switch (immediate.kind) {
        case ImmediateKind::SIGNED:
            return TryAppendNumber(std::get<int64_t>(immediate.value), maxBytes, formatted);
        case ImmediateKind::UNSIGNED:
            return TryAppendNumber(std::get<uint64_t>(immediate.value), maxBytes, formatted);
        case ImmediateKind::FLOAT:
            return TryAppendNumber(std::get<double>(immediate.value), maxBytes, formatted);
    }
    return false;
}

template <class String>
bool FormatIdOperandImpl(const IdOperand &id, uint64_t maxBytes, String *formatted)
{
    if (formatted == nullptr) {
        return false;
    }
    formatted->clear();
    switch (id.kind) {
        case IdOperandKind::STRING:
            return TryAppend("\"", maxBytes, formatted) && AppendEscaped(id.resolvedText, maxBytes, formatted) &&
                   TryAppend("\"", maxBytes, formatted);
        case IdOperandKind::METHOD:
            if (!AppendEscaped(id.resolvedText, maxBytes, formatted) || !TryAppend("@", maxBytes, formatted)) {
                return false;
            }
            return id.resolvedOffset.has_value()
                       ? TryAppendPc(static_cast<uint32_t>(*id.resolvedOffset), maxBytes, formatted)
                       : TryAppend("?", maxBytes, formatted);
        case IdOperandKind::LITERAL_ARRAY:
            if (!TryAppend("literal-array@", maxBytes, formatted)) {
                return false;
            }
            return id.resolvedOffset.has_value()
                       ? TryAppendPc(static_cast<uint32_t>(*id.resolvedOffset), maxBytes, formatted)
                       : TryAppend("?", maxBytes, formatted);
        case IdOperandKind::UNKNOWN:
            return TryAppend("id", maxBytes, formatted) && TryAppendNumber(id.rawValue, maxBytes, formatted);
    }
    return false;
}

bool DisassemblyFormatter::FormatIdOperand(const IdOperand &id, uint64_t maxBytes, std::string *formatted)
{
    return FormatIdOperandImpl(id, maxBytes, formatted);
}

bool DisassemblyFormatter::FormatIdOperand(const IdOperand &id, uint64_t maxBytes, ModelString *formatted)
{
    return FormatIdOperandImpl(id, maxBytes, formatted);
}

std::string DisassemblyFormatter::FormatIdOperand(const IdOperand &id)
{
    std::string formatted;
    static_cast<void>(FormatIdOperand(id, std::numeric_limits<uint64_t>::max(), &formatted));
    return formatted;
}

std::string DisassemblyFormatter::FormatInstruction(const InstructionInfo &instruction)
{
    if (instruction.operands.empty()) {
        return {instruction.mnemonic.data(), instruction.mnemonic.size()};
    }
    std::string formatted(instruction.mnemonic.data(), instruction.mnemonic.size());
    formatted.push_back(' ');
    formatted.append(instruction.operands.data(), instruction.operands.size());
    return formatted;
}

std::string DisassemblyFormatter::FormatJumpTarget(const std::optional<uint32_t> &target)
{
    return target.has_value() ? FormatPc(*target) : std::string {};
}

std::string DisassemblyFormatter::FormatInstructionDetails(const InstructionInfo &instruction, uint32_t vregCount)
{
    std::ostringstream output;
    output << "Encoded byte size: " << instruction.byteSize << '\n';
    output << "Numeric opcode: " << FormatOpcode(instruction.numericOpcode) << '\n';
    output << "Registers: ";
    AppendRegisters(&output, instruction.registers, vregCount);
    output << "\nImmediates: ";
    AppendImmediates(&output, instruction.immediates);
    output << "\nIDs: ";
    AppendDetailedIds(&output, instruction.ids);
    output << "\nABC file range: ";
    if (instruction.fileRange.size > std::numeric_limits<uint64_t>::max() - instruction.fileRange.offset) {
        output << "overflow";
    } else {
        output << '[' << FormatAbcOffset(instruction.fileRange.offset) << ", "
               << FormatAbcOffset(instruction.fileRange.offset + instruction.fileRange.size) << ')';
    }
    output << "\nCan throw: " << (instruction.canThrow ? "yes" : "no");
    output << "\nTerminator: " << (instruction.isTerminator ? "yes" : "no");
    return output.str();
}

std::string DisassemblyFormatter::FormatTooltip(const InstructionInfo &instruction)
{
    std::ostringstream output;
    output << "Size: " << instruction.byteSize << " bytes\n";
    output << "Numeric opcode: " << FormatOpcode(instruction.numericOpcode) << "\n";
    if (instruction.fileRange.size > std::numeric_limits<uint64_t>::max() - instruction.fileRange.offset) {
        output << "ABC range: overflow\n";
    } else {
        output << "ABC range: [0x" << std::hex << instruction.fileRange.offset << ", 0x"
               << instruction.fileRange.offset + instruction.fileRange.size << ")\n";
    }
    output << std::dec << "Immediates: ";
    AppendImmediates(&output, instruction.immediates);
    output << "\nIDs: ";
    AppendIds(&output, instruction.ids);
    output << "\nCan throw: " << (instruction.canThrow ? "yes" : "no");
    output << "\nTerminator: " << (instruction.isTerminator ? "yes" : "no");
    return output.str();
}

}  // namespace panda::bytecode_explorer
