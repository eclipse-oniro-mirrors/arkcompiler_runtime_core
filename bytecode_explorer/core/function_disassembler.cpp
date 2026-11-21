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

#include "core/function_disassembler.h"

#include "core/format/disassembly_formatter.h"

#include "bytecode_instruction-inl.h"
#include "code_data_accessor.h"
#include "literal_data_accessor.h"
#include "method_data_accessor-inl.h"
#include "utils/bit_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

constexpr uint64_t MAX_ULEB32_SIZE = 5U;
constexpr uint64_t METHOD_FIXED_HEADER_SIZE = sizeof(panda_file::File::Index) * 2U + sizeof(uint32_t);

DisassemblyResult Failure(DisassemblyStatus status, std::string diagnostic)
{
    return {status, nullptr, std::move(diagnostic)};
}

bool IsRangeInside(uint64_t offset, uint64_t amount, uint64_t size)
{
    return offset <= size && amount <= size - offset;
}

bool Advance(uint64_t amount, uint64_t size, uint64_t *cursor)
{
    if (!IsRangeInside(*cursor, amount, size)) {
        return false;
    }
    *cursor += amount;
    return true;
}

bool ReadUleb32(const uint8_t *data, uint64_t size, uint64_t *cursor, uint32_t *value)
{
    uint32_t result = 0U;
    for (uint64_t byteIndex = 0U; byteIndex < MAX_ULEB32_SIZE; ++byteIndex) {
        if (*cursor >= size) {
            return false;
        }
        const auto byte = data[static_cast<size_t>((*cursor)++)];
        if (byteIndex == MAX_ULEB32_SIZE - 1U && (byte & 0xF0U) != 0U) {
            return false;
        }
        result |= static_cast<uint32_t>(byte & 0x7FU) << (byteIndex * 7U);
        if ((byte & 0x80U) == 0U) {
            *value = result;
            return true;
        }
    }
    return false;
}

uint32_t ReadUint32(const uint8_t *data)
{
    uint32_t value = 0U;
    uint32_t shift = 0U;
    for (size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<uint32_t>(data[index]) << shift;
        shift += 8U;
    }
    return value;
}

bool AddWithinLimit(uint64_t amount, uint64_t limit, uint64_t *total)
{
    if (*total > limit || amount > limit - *total) {
        return false;
    }
    *total += amount;
    return true;
}

bool TryAdd(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (right > std::numeric_limits<uint64_t>::max() - left) {
        return false;
    }
    *sum = left + right;
    return true;
}

bool TryMultiply(uint64_t left, uint64_t right, uint64_t *product)
{
    if (left != 0U && right > std::numeric_limits<uint64_t>::max() / left) {
        return false;
    }
    *product = left * right;
    return true;
}

bool TryAddPcRelative(uint32_t pc, int64_t displacement, int64_t *target)
{
    if (displacement > 0 && displacement > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(pc)) {
        return false;
    }
    *target = static_cast<int64_t>(pc) + displacement;
    return true;
}

uint64_t SaturatingAdd(uint64_t left, uint64_t right)
{
    uint64_t sum = 0U;
    return TryAdd(left, right, &sum) ? sum : std::numeric_limits<uint64_t>::max();
}

uint64_t SaturatingMultiply(uint64_t left, uint64_t right)
{
    uint64_t product = 0U;
    return TryMultiply(left, right, &product) ? product : std::numeric_limits<uint64_t>::max();
}

struct CodeLayout {
    explicit CodeLayout(pmr::memory_resource *resource) : exceptionBoundaries(resource), tryRegions(resource) {}

    uint64_t codeOffset {0U};
    uint64_t instructionsOffset {0U};
    uint32_t vregCount {0U};
    uint32_t argumentCount {0U};
    uint32_t codeSize {0U};
    uint32_t tryCount {0U};
    ModelVector<uint32_t> exceptionBoundaries;
    ModelVector<TryRegionInfo> tryRegions;
};

struct LayoutResult {
    explicit LayoutResult(pmr::memory_resource *resource) : layout(resource) {}

    LayoutResult(DisassemblyStatus resultStatus, CodeLayout resultLayout, std::string resultDiagnostic)
        : status(resultStatus), layout(std::move(resultLayout)), diagnostic(std::move(resultDiagnostic))
    {
    }

    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    CodeLayout layout;
    std::string diagnostic;
};

struct ExceptionScan {
    DisassemblyStatus status {DisassemblyStatus::SUCCESS};
    uint64_t catchCount {0U};
    std::string diagnostic;
};

// Byte cursor for one exception-table scan: the table bytes, their extent and the read position.
struct ExceptionTableScan {
    const uint8_t *data {nullptr};
    uint64_t size {0U};
    uint64_t cursor {0U};
};

// Location and uleb header fields of one code item under validation.
struct CodeItemHeader {
    const uint8_t *data {nullptr};
    uint64_t available {0U};
    uint64_t cursor {0U};
    uint32_t vregs {0U};
    uint32_t args {0U};
    uint32_t codeSize {0U};
    uint32_t tryCount {0U};
};

// Exception-model capture outputs of one code layout, sharing one resource.
struct ExceptionRegions {
    explicit ExceptionRegions(pmr::memory_resource *resource) : boundaries(resource), tryRegions(resource) {}

    ModelVector<uint32_t> boundaries;
    ModelVector<TryRegionInfo> tryRegions;
};

// Shared state of one exception-table walk: the moving cursor, the code extent, cooperative
// cancellation and the nullable model outputs (both stay null while probing).
struct ExceptionTableWalk {
    ExceptionTableScan scan;
    uint32_t codeSize {0U};
    const CancellationToken *cancellation {nullptr};
    ModelVector<uint32_t> *boundaries {nullptr};
    ModelVector<TryRegionInfo> *tryRegions {nullptr};
};

// Reads and validates one try-block header: its start, length and catch-block count.
bool ReadTryBlockHeader(ExceptionTableWalk *walk, uint32_t *startPc, uint32_t *length, uint32_t *catchCount)
{
    if (!ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, startPc) ||
        !ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, length) ||
        !ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, catchCount) || *startPc >= walk->codeSize ||
        *length > walk->codeSize - *startPc) {
        return false;
    }
    return true;
}

// Records one try block's region and boundaries when the model outputs are present; returns the
// newly registered region, or null while probing.
TryRegionInfo *RegisterTryBlock(ExceptionTableWalk *walk, uint32_t startPc, uint32_t length, uint32_t catchCount)
{
    TryRegionInfo *tryRegion = nullptr;
    if (walk->tryRegions != nullptr) {
        auto &tryRegions = *walk->tryRegions;
        tryRegions.emplace_back(tryRegions.get_allocator().resource());
        tryRegion = &tryRegions.back();
        tryRegion->startPc = startPc;
        tryRegion->endPc = startPc + length;
        tryRegion->handlers.reserve(catchCount);
    }
    if (walk->boundaries != nullptr) {
        walk->boundaries->push_back(startPc);
        walk->boundaries->push_back(startPc + length);
    }
    return tryRegion;
}

// Scans the catch-block entries of one try block, recording handlers and boundaries.
ExceptionScan ScanCatchBlocks(ExceptionTableWalk *walk, uint32_t catchCount, TryRegionInfo *tryRegion,
                              uint64_t totalCatches)
{
    for (uint32_t catchIndex = 0U; catchIndex < catchCount; ++catchIndex) {
        if (walk->cancellation->IsCancellationRequested()) {
            return {DisassemblyStatus::CANCELLED, totalCatches, "Disassembly was cancelled"};
        }
        uint32_t typeIndex = 0U;
        uint32_t handlerPc = 0U;
        uint32_t handlerSize = 0U;
        if (!ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, &typeIndex) ||
            !ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, &handlerPc) ||
            !ReadUleb32(walk->scan.data, walk->scan.size, &walk->scan.cursor, &handlerSize) ||
            handlerPc >= walk->codeSize || handlerSize > walk->codeSize - handlerPc) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, totalCatches, "Catch block is malformed"};
        }
        if (walk->boundaries != nullptr) {
            walk->boundaries->push_back(handlerPc);
            walk->boundaries->push_back(handlerPc + handlerSize);
        }
        if (tryRegion != nullptr) {
            tryRegion->handlers.push_back({typeIndex, handlerPc, handlerSize, catchIndex});
        }
    }
    return {DisassemblyStatus::SUCCESS, totalCatches, {}};
}

// Walks the exception table: probes it when regions is null and captures the model outputs otherwise.
ExceptionScan ScanExceptionTable(const CodeItemHeader &header, uint64_t exceptionCursor,
                                 const DisassemblyLimits &limits, const CancellationToken &cancellation,
                                 ExceptionRegions *regions)
{
    constexpr uint64_t minTryBlockSize = 3U;
    constexpr uint64_t minCatchBlockSize = 3U;
    if (header.tryCount > limits.maxTryBlockCount) {
        return {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, 0U, "Try-block count exceeds the configured limit"};
    }
    if (exceptionCursor > header.available ||
        header.tryCount > (header.available - exceptionCursor) / minTryBlockSize) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, 0U, "Exception table is truncated"};
    }

    ExceptionTableWalk walk {{header.data, header.available, exceptionCursor},
                             header.codeSize,
                             &cancellation,
                             regions != nullptr ? &regions->boundaries : nullptr,
                             regions != nullptr ? &regions->tryRegions : nullptr};
    uint64_t totalCatches = 0U;
    for (uint32_t tryIndex = 0U; tryIndex < header.tryCount; ++tryIndex) {
        if (cancellation.IsCancellationRequested()) {
            return {DisassemblyStatus::CANCELLED, totalCatches, "Disassembly was cancelled"};
        }
        uint32_t startPc = 0U;
        uint32_t length = 0U;
        uint32_t catchCount = 0U;
        if (!ReadTryBlockHeader(&walk, &startPc, &length, &catchCount)) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, totalCatches, "Try block is malformed"};
        }
        TryRegionInfo *tryRegion = RegisterTryBlock(&walk, startPc, length, catchCount);
        if (catchCount > limits.maxTotalCatchCount - std::min(totalCatches, limits.maxTotalCatchCount)) {
            return {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, totalCatches,
                    "Total catch-block count exceeds the configured limit"};
        }
        totalCatches += catchCount;
        if (catchCount > (walk.scan.size - walk.scan.cursor) / minCatchBlockSize) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, totalCatches, "Catch table is truncated"};
        }
        const ExceptionScan catchScan = ScanCatchBlocks(&walk, catchCount, tryRegion, totalCatches);
        if (catchScan.status != DisassemblyStatus::SUCCESS) {
            return catchScan;
        }
    }
    return {DisassemblyStatus::SUCCESS, totalCatches, {}};
}

// Reads the uleb header fields of one code item and validates the code extent.
bool ReadCodeItemHeaderFields(CodeItemHeader *header)
{
    return ReadUleb32(header->data, header->available, &header->cursor, &header->vregs) &&
           ReadUleb32(header->data, header->available, &header->cursor, &header->args) &&
           ReadUleb32(header->data, header->available, &header->cursor, &header->codeSize) &&
           ReadUleb32(header->data, header->available, &header->cursor, &header->tryCount) && header->codeSize != 0U &&
           IsRangeInside(header->cursor, header->codeSize, header->available);
}

// Result of probing the exception table: status, diagnostic and the validated boundary count.
struct ExceptionProbe {
    DisassemblyStatus status {DisassemblyStatus::SUCCESS};
    uint64_t boundaryCount {0U};
    std::string diagnostic;
};

// Probes the exception table without capturing model outputs and validates that the
// exception-boundary model fits the configured limits.
ExceptionProbe ProbeExceptionTable(const CodeItemHeader &header, uint64_t exceptionCursor,
                                   const DisassemblyLimits &limits, const CancellationToken &cancellation)
{
    const auto scan = ScanExceptionTable(header, exceptionCursor, limits, cancellation, nullptr);
    if (scan.status != DisassemblyStatus::SUCCESS) {
        return {scan.status, 0U, scan.diagnostic};
    }
    uint64_t exceptionCount = 0U;
    uint64_t boundaryCount = 0U;
    uint64_t boundaryBytes = 0U;
    if (!TryAdd(header.tryCount, scan.catchCount, &exceptionCount) ||
        !TryMultiply(2U, exceptionCount, &boundaryCount) ||
        !TryMultiply(boundaryCount, sizeof(uint32_t), &boundaryBytes) || boundaryBytes > limits.maxModelBytes) {
        return {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, 0U,
                "Exception-boundary model exceeds the configured limit"};
    }
    return {DisassemblyStatus::SUCCESS, boundaryCount, {}};
}

// Assembles the validated code layout from the header fields and the captured exception regions.
CodeLayout AssembleCodeLayout(uint32_t codeOffset, uint64_t instructionsOffset, const CodeItemHeader &header,
                              ExceptionRegions regions, pmr::memory_resource *resource)
{
    CodeLayout layout(resource);
    layout.codeOffset = codeOffset;
    layout.instructionsOffset = instructionsOffset;
    layout.vregCount = header.vregs;
    layout.argumentCount = header.args;
    layout.codeSize = header.codeSize;
    layout.tryCount = header.tryCount;
    layout.exceptionBoundaries = std::move(regions.boundaries);
    layout.tryRegions = std::move(regions.tryRegions);
    return layout;
}

LayoutResult ReadCodeLayout(const panda_file::File &file, uint32_t codeOffset, const DisassemblyLimits &limits,
                            const CancellationToken &cancellation, pmr::memory_resource *resource)
{
    if (cancellation.IsCancellationRequested()) {
        return {DisassemblyStatus::CANCELLED, CodeLayout(resource), "Disassembly was cancelled"};
    }
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (fileSize > std::numeric_limits<size_t>::max() || !IsRangeInside(codeOffset, 4U, fileSize)) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, CodeLayout(resource),
                "METHOD code item is outside the ABC file"};
    }
    CodeItemHeader header;
    header.data = file.GetBase() + static_cast<size_t>(codeOffset);  // NOLINT
    header.available = fileSize - codeOffset;
    if (!ReadCodeItemHeaderFields(&header)) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, CodeLayout(resource),
                "METHOD code item is truncated or malformed"};
    }
    if (header.codeSize > limits.maxCodeBytes) {
        return {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, CodeLayout(resource),
                "Code byte size exceeds the configured limit"};
    }
    const uint64_t instructionsOffset = static_cast<uint64_t>(codeOffset) + header.cursor;
    uint64_t exceptionCursor = header.cursor;
    if (!Advance(header.codeSize, header.available, &exceptionCursor)) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, CodeLayout(resource),
                "METHOD instructions cross the ABC file boundary"};
    }
    const auto probe = ProbeExceptionTable(header, exceptionCursor, limits, cancellation);
    if (probe.status != DisassemblyStatus::SUCCESS) {
        return {probe.status, CodeLayout(resource), probe.diagnostic};
    }
    ExceptionRegions regions(resource);
    if (probe.boundaryCount > static_cast<uint64_t>(regions.boundaries.max_size())) {
        return {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, CodeLayout(resource),
                "Exception-boundary count exceeds host vector capacity"};
    }
    regions.boundaries.reserve(static_cast<size_t>(probe.boundaryCount));
    regions.tryRegions.reserve(header.tryCount);
    const auto captured = ScanExceptionTable(header, exceptionCursor, limits, cancellation, &regions);
    if (captured.status != DisassemblyStatus::SUCCESS) {
        return {captured.status, CodeLayout(resource), captured.diagnostic};
    }
    return {DisassemblyStatus::SUCCESS,
            AssembleCodeLayout(codeOffset, instructionsOffset, header, std::move(regions), resource),
            {}};
}

DisassemblyResult ValidateInstruction(const BytecodeInstructionSafe &instruction, uint32_t pc, uint32_t remaining)
{
    if (!instruction.IsValid() || !instruction.IsPrimaryOpcodeValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Invalid bytecode primary opcode at PC " + std::to_string(pc));
    }
    if (instruction.IsPrefixed() && remaining < 2U) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Truncated prefixed opcode at PC " + std::to_string(pc));
    }
    if (!instruction.IsOpcodeValid() || !instruction.IsValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Invalid bytecode opcode at PC " + std::to_string(pc));
    }
    const auto instructionSize = instruction.GetSize();
    if (!instruction.IsValid() || instructionSize == 0U || instructionSize > remaining) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Instruction crosses the code range at PC " + std::to_string(pc));
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

class BoundedStringBuffer final : public std::streambuf {
public:
    BoundedStringBuffer(uint64_t limit, pmr::memory_resource *resource)
        : value_(resource), limit_(std::min<uint64_t>(limit, value_.max_size()))
    {
    }

    const ModelString &Value() const
    {
        return value_;
    }

    ModelString TakeValue()
    {
        return std::move(value_);
    }

    bool Exceeded() const
    {
        return exceeded_;
    }

protected:
    int_type overflow(int_type character) override
    {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        if (value_.size() >= limit_ || value_.size() >= value_.max_size()) {
            exceeded_ = true;
            return traits_type::eof();
        }
        value_.push_back(traits_type::to_char_type(character));
        return character;
    }

    std::streamsize xsputn(const char *characters, std::streamsize count) override
    {
        if (count < 0 || static_cast<uint64_t>(count) > limit_ - std::min<uint64_t>(value_.size(), limit_) ||
            static_cast<uint64_t>(count) > value_.max_size() - value_.size()) {
            exceeded_ = true;
            return 0;
        }
        value_.append(characters, static_cast<size_t>(count));
        return count;
    }

private:
    ModelString value_;
    uint64_t limit_ {0U};
    bool exceeded_ {false};
};

struct TextResolution {
    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    ModelString text;
    std::string diagnostic;
};

struct IdResolution {
    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    IdOperand operand;
    std::string diagnostic;
};

struct CurrentMethodIndex {
    uint64_t entriesOffset {0U};
    uint32_t size {0U};
};

struct CurrentMethodIndexResult {
    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    CurrentMethodIndex index;
    std::string diagnostic;
};

struct LiteralValidationContext {
    explicit LiteralValidationContext(pmr::memory_resource *resource) : inventory(resource), validatedOffsets(resource)
    {
    }

    bool hasInventory {false};
    ModelVector<uint32_t> inventory;
    pmr::set<uint32_t> validatedOffsets;
};

struct LiteralValidationContextResult {
    explicit LiteralValidationContextResult(pmr::memory_resource *resource) : context(resource) {}

    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    LiteralValidationContext context;
    std::string diagnostic;
};

CurrentMethodIndexResult ReadCurrentMethodIndex(const panda_file::File &file, uint64_t methodOffset,
                                                const CancellationToken &cancellation)
{
    const auto *fileHeader = file.GetHeader();
    const uint64_t fileSize = fileHeader->file_size;
    uint64_t headersBytes = 0U;
    if (!TryMultiply(fileHeader->num_indexes, sizeof(panda_file::File::IndexHeader), &headersBytes) ||
        !IsRangeInside(fileHeader->index_section_off, headersBytes, fileSize)) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "ABC index-header table is outside the file"};
    }

    for (uint32_t headerIndex = 0U; headerIndex < fileHeader->num_indexes; ++headerIndex) {
        if (cancellation.IsCancellationRequested()) {
            return {DisassemblyStatus::CANCELLED, {}, "Disassembly was cancelled"};
        }
        const uint64_t headerOffset = static_cast<uint64_t>(fileHeader->index_section_off) +
                                      static_cast<uint64_t>(headerIndex) * sizeof(panda_file::File::IndexHeader);
        const auto *header = file.GetBase() + static_cast<size_t>(headerOffset);  // NOLINT
        const auto start = ReadUint32(header + offsetof(panda_file::File::IndexHeader, start));
        const auto end = ReadUint32(header + offsetof(panda_file::File::IndexHeader, end));
        if (start >= end || end > fileSize) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "ABC index header has a malformed entity span"};
        }
        if (methodOffset < start || methodOffset >= end) {
            continue;
        }
        const auto methodIndexSize = ReadUint32(header + offsetof(panda_file::File::IndexHeader, method_idx_size));
        const auto methodIndexOffset = ReadUint32(header + offsetof(panda_file::File::IndexHeader, method_idx_off));
        uint64_t methodIndexBytes = 0U;
        if (!TryMultiply(methodIndexSize, sizeof(panda_file::File::EntityId), &methodIndexBytes) ||
            !IsRangeInside(methodIndexOffset, methodIndexBytes, fileSize)) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "Current METHOD index table is outside the file"};
        }
        return {DisassemblyStatus::SUCCESS, {methodIndexOffset, methodIndexSize}, {}};
    }
    return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "METHOD has no containing index span"};
}

LiteralValidationContextResult BuildLiteralValidationContext(const panda_file::File &file,
                                                             const CancellationToken &cancellation,
                                                             pmr::memory_resource *resource)
{
    LiteralValidationContextResult result(resource);
    const auto *header = file.GetHeader();
    if (!panda_file::ContainsLiteralArrayInHeader(header->version)) {
        result.status = DisassemblyStatus::SUCCESS;
        return result;
    }
    uint64_t inventoryBytes = 0U;
    if (!TryMultiply(header->num_literalarrays, sizeof(panda_file::File::EntityId), &inventoryBytes) ||
        !IsRangeInside(header->literalarray_idx_off, inventoryBytes, header->file_size)) {
        result.diagnostic = "ABC literal-array inventory is outside the file";
        return result;
    }
    result.context.hasInventory = true;
    result.context.inventory.reserve(header->num_literalarrays);
    for (uint32_t index = 0U; index < header->num_literalarrays; ++index) {
        constexpr uint32_t cancellationInterval = 256U;
        if (index % cancellationInterval == 0U && cancellation.IsCancellationRequested()) {
            result.status = DisassemblyStatus::CANCELLED;
            result.diagnostic = "Disassembly was cancelled";
            return result;
        }
        const uint64_t entryOffset = static_cast<uint64_t>(header->literalarray_idx_off) +
                                     static_cast<uint64_t>(index) * sizeof(panda_file::File::EntityId);
        const auto literalOffset = ReadUint32(file.GetBase() + static_cast<size_t>(entryOffset));  // NOLINT
        if (!panda_file::File::EntityId(literalOffset).IsValid() || literalOffset >= header->file_size) {
            result.diagnostic = "ABC literal-array inventory contains an invalid entity offset";
            return result;
        }
        result.context.inventory.push_back(literalOffset);
    }
    std::sort(result.context.inventory.begin(), result.context.inventory.end());
    result.status = DisassemblyStatus::SUCCESS;
    return result;
}

bool ResolveCurrentMethodIndex(const panda_file::File &file, const CurrentMethodIndex &currentIndex, uint32_t rawIndex,
                               uint32_t *resolvedOffset, std::string *diagnostic)
{
    if (rawIndex == std::numeric_limits<uint16_t>::max()) {
        *diagnostic = "Instruction ID uses the reserved 16-bit index value";
        return false;
    }
    if (rawIndex >= currentIndex.size) {
        *diagnostic = "Instruction ID index is outside the current METHOD index span";
        return false;
    }
    const uint64_t entryOffset =
        currentIndex.entriesOffset + static_cast<uint64_t>(rawIndex) * sizeof(panda_file::File::EntityId);
    const auto targetOffset = ReadUint32(file.GetBase() + static_cast<size_t>(entryOffset));  // NOLINT
    if (!panda_file::File::EntityId(targetOffset).IsValid() || targetOffset >= file.GetHeader()->file_size) {
        *diagnostic = "Instruction ID index resolves outside the ABC file";
        return false;
    }
    *resolvedOffset = targetOffset;
    return true;
}

// Value width of one scalar literal-array tag; returns 0 for packed-array and unknown tags.
uint64_t ScalarLiteralTagWidth(panda_file::LiteralTag tag)
{
    switch (tag) {
        case panda_file::LiteralTag::TAGVALUE:
        case panda_file::LiteralTag::BOOL:
        case panda_file::LiteralTag::BUILTINTYPEINDEX:
        case panda_file::LiteralTag::ACCESSOR:
        case panda_file::LiteralTag::NULLVALUE:
            return sizeof(uint8_t);
        case panda_file::LiteralTag::METHODAFFILIATE:
            return sizeof(uint16_t);
        case panda_file::LiteralTag::INTEGER:
        case panda_file::LiteralTag::FLOAT:
        case panda_file::LiteralTag::STRING:
        case panda_file::LiteralTag::METHOD:
        case panda_file::LiteralTag::GENERATORMETHOD:
        case panda_file::LiteralTag::GETTER:
        case panda_file::LiteralTag::SETTER:
        case panda_file::LiteralTag::ASYNCGENERATORMETHOD:
        case panda_file::LiteralTag::LITERALBUFFERINDEX:
        case panda_file::LiteralTag::LITERALARRAY:
        case panda_file::LiteralTag::ETS_IMPLEMENTS:
            return sizeof(uint32_t);
        case panda_file::LiteralTag::DOUBLE:
            return sizeof(uint64_t);
        default:
            return 0U;
    }
}

// Value width of one packed-array literal-array tag; returns 0 for scalar and unknown tags.
uint64_t PackedArrayLiteralTagWidth(panda_file::LiteralTag tag)
{
    switch (tag) {
        case panda_file::LiteralTag::ARRAY_U1:
        case panda_file::LiteralTag::ARRAY_U8:
        case panda_file::LiteralTag::ARRAY_I8:
            return sizeof(uint8_t);
        case panda_file::LiteralTag::ARRAY_U16:
        case panda_file::LiteralTag::ARRAY_I16:
            return sizeof(uint16_t);
        case panda_file::LiteralTag::ARRAY_U32:
        case panda_file::LiteralTag::ARRAY_I32:
        case panda_file::LiteralTag::ARRAY_F32:
        case panda_file::LiteralTag::ARRAY_STRING:
            return sizeof(uint32_t);
        case panda_file::LiteralTag::ARRAY_U64:
        case panda_file::LiteralTag::ARRAY_I64:
        case panda_file::LiteralTag::ARRAY_F64:
            return sizeof(uint64_t);
        default:
            return 0U;
    }
}

// Validates the length and payload of one packed literal array, advancing the cursor past it.
DisassemblyResult ValidatePackedArrayPayload(const panda_file::File &file, uint32_t valueCount, uint32_t itemIndex,
                                             uint64_t valueWidth, uint64_t *cursor)
{
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (itemIndex >= valueCount || !IsRangeInside(*cursor, sizeof(uint32_t), fileSize)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Packed literal-array length is truncated or missing");
    }
    const auto elementCount = ReadUint32(file.GetBase() + static_cast<size_t>(*cursor));  // NOLINT
    *cursor += sizeof(uint32_t);
    const uint64_t remainingItems = static_cast<uint64_t>(valueCount) - itemIndex;
    uint64_t requiredItems = 0U;
    if (!TryAdd(elementCount, 1U, &requiredItems) || requiredItems != remainingItems) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Packed literal-array length disagrees with the remaining item count");
    }
    uint64_t payloadBytes = 0U;
    if (!TryMultiply(elementCount, valueWidth, &payloadBytes) || !Advance(payloadBytes, fileSize, cursor)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Packed literal-array payload is truncated for its element width");
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Walks the tagged values of one literal array; returns SUCCESS when the values are well-formed
// and the caller may record the array as validated.
DisassemblyResult ValidateLiteralArrayItems(const panda_file::File &file, uint32_t valueCount, uint64_t fileSize,
                                            uint64_t cursor, const CancellationToken &cancellation)
{
    for (uint32_t itemIndex = 0U; itemIndex < valueCount;) {
        constexpr uint32_t cancellationInterval = 1024U;
        if (itemIndex != 0U && itemIndex % cancellationInterval == 0U && cancellation.IsCancellationRequested()) {
            return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
        }
        if (!IsRangeInside(cursor, sizeof(uint8_t), fileSize)) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Literal-array tag is truncated");
        }
        const auto tag = static_cast<panda_file::LiteralTag>(file.GetBase()[static_cast<size_t>(cursor++)]);
        ++itemIndex;
        const auto scalarWidth = ScalarLiteralTagWidth(tag);
        const auto packedWidth = PackedArrayLiteralTagWidth(tag);
        if (scalarWidth == 0U && packedWidth == 0U) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Literal-array tag is not recognized");
        }
        if (packedWidth != 0U) {
            return ValidatePackedArrayPayload(file, valueCount, itemIndex, packedWidth, &cursor);
        }
        if (itemIndex >= valueCount) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Literal-array has an unmatched tag without a value");
        }
        if (!Advance(scalarWidth, fileSize, &cursor)) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                           "Literal-array is truncated for its literal-array tag value width");
        }
        ++itemIndex;
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

DisassemblyResult ValidateLiteralArray(const panda_file::File &file, LiteralValidationContext *context,
                                       uint32_t resolvedOffset, const CancellationToken &cancellation)
{
    if (context->hasInventory &&
        !std::binary_search(context->inventory.begin(), context->inventory.end(), resolvedOffset)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Literal-array ID is not a member of the ABC literal-array inventory");
    }
    if (context->validatedOffsets.find(resolvedOffset) != context->validatedOffsets.end()) {
        return {DisassemblyStatus::SUCCESS, nullptr, {}};
    }
    const auto success = [context, resolvedOffset]() {
        context->validatedOffsets.insert(resolvedOffset);
        return DisassemblyResult {DisassemblyStatus::SUCCESS, nullptr, {}};
    };

    constexpr uint64_t headerSize = sizeof(uint32_t);
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (!IsRangeInside(resolvedOffset, headerSize, fileSize)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Literal-array ID resolves outside the ABC file");
    }
    const auto valueCount = ReadUint32(file.GetBase() + static_cast<size_t>(resolvedOffset));  // NOLINT
    uint64_t cursor = static_cast<uint64_t>(resolvedOffset) + headerSize;
    if (valueCount > fileSize - cursor) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Literal-array has a malicious or truncated value count");
    }
    const auto items = ValidateLiteralArrayItems(file, valueCount, fileSize, cursor, cancellation);
    if (items.status != DisassemblyStatus::SUCCESS) {
        return items;
    }
    return success();
}

bool IsContinuationByte(uint8_t byte)
{
    return (byte & 0xc0U) == 0x80U;
}

bool AppendUtf8(uint32_t codePoint, uint64_t maxTextBytes, ModelString *text)
{
    std::array<char, 4U> encoded {};
    char *next = encoded.data();
    size_t size = 0U;
    if (codePoint <= 0x7fU) {
        *next++ = static_cast<char>(codePoint);
        size = 1U;
    } else if (codePoint <= 0x7ffU) {
        *next++ = static_cast<char>(0xc0U | (codePoint >> 6U));
        *next++ = static_cast<char>(0x80U | (codePoint & 0x3fU));
        size = 2U;
    } else if (codePoint <= 0xffffU) {
        *next++ = static_cast<char>(0xe0U | (codePoint >> 12U));
        *next++ = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU));
        *next++ = static_cast<char>(0x80U | (codePoint & 0x3fU));
        size = 3U;
    } else {
        *next++ = static_cast<char>(0xf0U | (codePoint >> 18U));
        *next++ = static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU));
        *next++ = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU));
        *next++ = static_cast<char>(0x80U | (codePoint & 0x3fU));
        size = 4U;
    }
    if (text->size() > maxTextBytes || size > maxTextBytes - text->size() || size > text->max_size() - text->size()) {
        return false;
    }
    text->append(encoded.data(), size);
    return true;
}

// One decoded MUTF-8 sequence: either a valid code point with its UTF-16 unit count and
// encoded size, or a disassembly failure carrying a diagnostic.
struct Mutf8Sequence {
    DisassemblyStatus status {DisassemblyStatus::SUCCESS};
    std::string diagnostic;
    uint32_t codePoint {0U};
    uint32_t utf16Units {1U};
    uint64_t sequenceSize {0U};
};

Mutf8Sequence Mutf8Failure(std::string diagnostic)
{
    Mutf8Sequence sequence;
    sequence.status = DisassemblyStatus::MALFORMED_BYTECODE;
    sequence.diagnostic = std::move(diagnostic);
    return sequence;
}

Mutf8Sequence DecodeTwoByteSequence(const panda_file::File &file, uint64_t cursor, uint64_t fileSize, uint8_t first)
{
    if (!IsRangeInside(cursor, 2U, fileSize)) {
        return Mutf8Failure("String ID contains a truncated MUTF-8 sequence");
    }
    const auto second = file.GetBase()[static_cast<size_t>(cursor + 1U)];
    if (!IsContinuationByte(second)) {
        return Mutf8Failure("String ID contains invalid MUTF-8");
    }
    Mutf8Sequence sequence;
    sequence.codePoint = (static_cast<uint32_t>(first & 0x1fU) << 6U) | (second & 0x3fU);
    if ((first == 0xc0U && second == 0x80U)) {
        sequence.codePoint = 0U;
    } else if (sequence.codePoint < 0x80U) {
        return Mutf8Failure("String ID contains overlong MUTF-8");
    }
    sequence.sequenceSize = 2U;
    return sequence;
}

Mutf8Sequence DecodeSurrogatePair(const panda_file::File &file, uint64_t cursor, uint64_t fileSize, uint32_t firstUnit)
{
    if (!IsRangeInside(cursor, 6U, fileSize)) {
        return Mutf8Failure("String ID contains a truncated MUTF-8 surrogate pair");
    }
    const auto fourth = file.GetBase()[static_cast<size_t>(cursor + 3U)];
    const auto fifth = file.GetBase()[static_cast<size_t>(cursor + 4U)];
    const auto sixth = file.GetBase()[static_cast<size_t>(cursor + 5U)];
    if (fourth < 0xe0U || fourth > 0xefU || !IsContinuationByte(fifth) || !IsContinuationByte(sixth)) {
        return Mutf8Failure("String ID contains invalid MUTF-8");
    }
    const auto secondUnit =
        (static_cast<uint32_t>(fourth & 0x0fU) << 12U) | (static_cast<uint32_t>(fifth & 0x3fU) << 6U) | (sixth & 0x3fU);
    if (secondUnit < 0xdc00U || secondUnit > 0xdfffU) {
        return Mutf8Failure("String ID contains an invalid MUTF-8 surrogate pair");
    }
    Mutf8Sequence sequence;
    sequence.codePoint = 0x10000U + ((firstUnit - 0xd800U) << 10U) + (secondUnit - 0xdc00U);
    sequence.utf16Units = 2U;
    sequence.sequenceSize = 6U;
    return sequence;
}

Mutf8Sequence DecodeThreeByteSequence(const panda_file::File &file, uint64_t cursor, uint64_t fileSize, uint8_t first)
{
    if (!IsRangeInside(cursor, 3U, fileSize)) {
        return Mutf8Failure("String ID contains a truncated MUTF-8 sequence");
    }
    const auto second = file.GetBase()[static_cast<size_t>(cursor + 1U)];
    const auto third = file.GetBase()[static_cast<size_t>(cursor + 2U)];
    if (!IsContinuationByte(second) || !IsContinuationByte(third)) {
        return Mutf8Failure("String ID contains invalid MUTF-8");
    }
    const auto firstUnit =
        (static_cast<uint32_t>(first & 0x0fU) << 12U) | (static_cast<uint32_t>(second & 0x3fU) << 6U) | (third & 0x3fU);
    if (firstUnit < 0x800U) {
        return Mutf8Failure("String ID contains overlong MUTF-8");
    }
    if (firstUnit >= 0xd800U && firstUnit <= 0xdbffU) {
        return DecodeSurrogatePair(file, cursor, fileSize, firstUnit);
    }
    if (firstUnit >= 0xdc00U && firstUnit <= 0xdfffU) {
        return Mutf8Failure("String ID contains an unpaired MUTF-8 surrogate");
    }
    Mutf8Sequence sequence;
    sequence.codePoint = firstUnit;
    sequence.sequenceSize = 3U;
    return sequence;
}

Mutf8Sequence DecodeFourByteSequence(const panda_file::File &file, uint64_t cursor, uint64_t fileSize, uint8_t first)
{
    if (!IsRangeInside(cursor, 4U, fileSize)) {
        return Mutf8Failure("String ID contains a truncated MUTF-8 sequence");
    }
    const auto second = file.GetBase()[static_cast<size_t>(cursor + 1U)];
    const auto third = file.GetBase()[static_cast<size_t>(cursor + 2U)];
    const auto fourth = file.GetBase()[static_cast<size_t>(cursor + 3U)];
    if (!IsContinuationByte(second) || !IsContinuationByte(third) || !IsContinuationByte(fourth)) {
        return Mutf8Failure("String ID contains invalid MUTF-8");
    }
    Mutf8Sequence sequence;
    sequence.codePoint = (static_cast<uint32_t>(first & 0x07U) << 18U) |
                         (static_cast<uint32_t>(second & 0x3fU) << 12U) | (static_cast<uint32_t>(third & 0x3fU) << 6U) |
                         (fourth & 0x3fU);
    if (sequence.codePoint < 0x10000U || sequence.codePoint > 0x10ffffU) {
        return Mutf8Failure("String ID contains invalid four-byte MUTF-8");
    }
    sequence.utf16Units = 2U;
    sequence.sequenceSize = 4U;
    return sequence;
}

// Decodes one MUTF-8 sequence starting at cursor; multi-byte forms are delegated to the
// per-width helpers above, which keep the original validation order and diagnostics.
Mutf8Sequence DecodeMutf8Sequence(const panda_file::File &file, uint64_t cursor, uint64_t fileSize)
{
    const auto first = file.GetBase()[static_cast<size_t>(cursor)];
    if (first <= 0x7fU) {
        Mutf8Sequence sequence;
        sequence.codePoint = first;
        sequence.sequenceSize = 1U;
        return sequence;
    }
    if (first >= 0xc0U && first <= 0xdfU) {
        return DecodeTwoByteSequence(file, cursor, fileSize, first);
    }
    if (first >= 0xe0U && first <= 0xefU) {
        return DecodeThreeByteSequence(file, cursor, fileSize, first);
    }
    if (first >= 0xf0U && first <= 0xf4U) {
        return DecodeFourByteSequence(file, cursor, fileSize, first);
    }
    return Mutf8Failure("String ID contains invalid MUTF-8");
}

TextResolution ResolveStringText(const panda_file::File &file, uint32_t offset, uint64_t maxTextBytes,
                                 const CancellationToken &cancellation, pmr::memory_resource *resource)
{
    const uint64_t fileSize = file.GetHeader()->file_size;
    if (!panda_file::File::EntityId(offset).IsValid() || offset >= fileSize) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "String ID resolves outside the ABC file"};
    }
    uint64_t cursor = offset;
    uint32_t taggedUtf16Length = 0U;
    if (!ReadUleb32(file.GetBase(), fileSize, &cursor, &taggedUtf16Length)) {
        return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "String ID has a malformed length"};
    }
    const auto expectedUtf16Length = taggedUtf16Length >> 1U;
    uint32_t utf16Length = 0U;
    uint64_t encodedBytes = 0U;
    uint64_t nextCancellationCheck = 1024U;
    ModelString decoded(resource);
    while (cursor < fileSize) {
        const auto first = file.GetBase()[static_cast<size_t>(cursor)];
        if (first == 0U) {
            if (utf16Length != expectedUtf16Length) {
                return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "String ID UTF-16 length does not match its data"};
            }
            return {DisassemblyStatus::SUCCESS, std::move(decoded), {}};
        }
        if (encodedBytes >= nextCancellationCheck && cancellation.IsCancellationRequested()) {
            return {DisassemblyStatus::CANCELLED, {}, "Disassembly was cancelled"};
        }
        if (encodedBytes >= nextCancellationCheck) {
            nextCancellationCheck = SaturatingAdd(encodedBytes, 1024U);
        }

        auto sequence = DecodeMutf8Sequence(file, cursor, fileSize);
        if (sequence.status != DisassemblyStatus::SUCCESS) {
            return {sequence.status, {}, std::move(sequence.diagnostic)};
        }

        if (sequence.utf16Units > expectedUtf16Length - std::min(utf16Length, expectedUtf16Length)) {
            return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "String ID UTF-16 length does not match its data"};
        }
        if (!AppendUtf8(sequence.codePoint, maxTextBytes, &decoded)) {
            return {
                DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, {}, "Resolved string text exceeds the configured limit"};
        }
        utf16Length += sequence.utf16Units;
        cursor += sequence.sequenceSize;
        encodedBytes += sequence.sequenceSize;
    }
    return {DisassemblyStatus::MALFORMED_BYTECODE, {}, "String ID is not terminated inside the ABC file"};
}

// Inputs shared by the instruction decode pipeline: the ABC file, the method-index services,
// the literal-validation context, the decode limits, cancellation and the model resource. All
// members are bound once per decoded method and stay constant across its operands.
struct DecodeContext {
    const panda_file::File *file {nullptr};
    const MethodIndex *methods {nullptr};
    const CurrentMethodIndex *currentIndex {nullptr};
    LiteralValidationContext *literalContext {nullptr};
    const DisassemblyLimits *limits {nullptr};
    const CancellationToken *cancellation {nullptr};
    pmr::memory_resource *resource {nullptr};
};

// One operand slot of the instruction being decoded: the owning instruction, the operand
// descriptor and the formatted-text budgets that apply to it.
struct OperandSlot {
    const BytecodeInstructionSafe *instruction {nullptr};
    const BytecodeOperandDescriptor *descriptor {nullptr};
    uint64_t textRemaining {0U};
    uint64_t operandsBytes {0U};
};

// Kind of an instruction ID operand as reported by the generated per-instruction flags.
enum class RecognizedIdKind { UNKNOWN, STRING, METHOD, LITERAL_ARRAY, CONFLICT };

// Builds the failure resolution like the original inline lambda did: a fresh operand
// bound to the decode resource, never the partially resolved one.
IdResolution IdFailure(const DecodeContext &context, DisassemblyStatus status, std::string diagnostic)
{
    return IdResolution {status, IdOperand(context.resource), std::move(diagnostic)};
}

RecognizedIdKind RecognizeIdKind(const OperandSlot &slot)
{
    const bool isString =
        slot.instruction->IsIdMatchFlag(slot.descriptor->typedIndex, BytecodeInstructionSafe::Flags::STRING_ID);
    const bool isMethod =
        slot.instruction->IsIdMatchFlag(slot.descriptor->typedIndex, BytecodeInstructionSafe::Flags::METHOD_ID);
    const bool isLiteralArray =
        slot.instruction->IsIdMatchFlag(slot.descriptor->typedIndex, BytecodeInstructionSafe::Flags::LITERALARRAY_ID);
    const auto recognizedKinds =
        static_cast<uint32_t>(isString) + static_cast<uint32_t>(isMethod) + static_cast<uint32_t>(isLiteralArray);
    if (recognizedKinds == 0U) {
        return RecognizedIdKind::UNKNOWN;
    }
    if (recognizedKinds != 1U) {
        return RecognizedIdKind::CONFLICT;
    }
    if (isString) {
        return RecognizedIdKind::STRING;
    }
    if (isMethod) {
        return RecognizedIdKind::METHOD;
    }
    return RecognizedIdKind::LITERAL_ARRAY;
}

IdResolution ResolveStringIdOperand(const DecodeContext &context, IdOperand operand, const OperandSlot &slot)
{
    operand.kind = IdOperandKind::STRING;
    auto text = ResolveStringText(*context.file, *operand.resolvedOffset, slot.textRemaining, *context.cancellation,
                                  context.resource);
    if (text.status != DisassemblyStatus::SUCCESS) {
        return IdFailure(context, text.status, std::move(text.diagnostic));
    }
    operand.resolvedText = std::move(text.text);
    return {DisassemblyStatus::SUCCESS, std::move(operand), {}};
}

IdResolution ResolveMethodIdOperand(const DecodeContext &context, IdOperand operand, const OperandSlot &slot)
{
    operand.kind = IdOperandKind::METHOD;
    if (context.methods->Find(*operand.resolvedOffset) == nullptr ||
        !IsRangeInside(*operand.resolvedOffset, METHOD_FIXED_HEADER_SIZE, context.file->GetHeader()->file_size)) {
        return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE,
                         "Method ID does not resolve to a METHOD entity");
    }
    const auto nameOffset = ReadUint32(context.file->GetBase() + static_cast<size_t>(*operand.resolvedOffset) +
                                       sizeof(panda_file::File::Index) * 2U);  // NOLINT
    auto text =
        ResolveStringText(*context.file, nameOffset, slot.textRemaining, *context.cancellation, context.resource);
    if (text.status != DisassemblyStatus::SUCCESS) {
        return IdFailure(context, text.status, "Method ID name is malformed: " + std::move(text.diagnostic));
    }
    operand.resolvedText = std::move(text.text);
    return {DisassemblyStatus::SUCCESS, std::move(operand), {}};
}

IdResolution ResolveLiteralArrayIdOperand(const DecodeContext &context, IdOperand operand)
{
    operand.kind = IdOperandKind::LITERAL_ARRAY;
    const auto validLiteral =
        ValidateLiteralArray(*context.file, context.literalContext, *operand.resolvedOffset, *context.cancellation);
    if (validLiteral.status != DisassemblyStatus::SUCCESS) {
        return IdFailure(context, validLiteral.status, validLiteral.diagnostic);
    }
    return {DisassemblyStatus::SUCCESS, std::move(operand), {}};
}

// Resolves one instruction ID operand: encoded-width handling, flag-based kind
// recognition, offset resolution and per-kind value resolution, preserving the
// original check order and diagnostics.
IdResolution ResolveIdOperand(const DecodeContext &context, const OperandSlot &slot, uint32_t rawValue)
{
    IdOperand operand(context.resource);
    operand.rawValue = rawValue;
    if (slot.descriptor->bitWidth == 16U) {
        operand.resolutionMode = IdResolutionMode::INDEX;
        if (rawValue == std::numeric_limits<uint16_t>::max()) {
            return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE,
                             "Instruction ID uses the reserved 16-bit index value");
        }
    } else if (slot.descriptor->bitWidth == 32U) {
        operand.resolutionMode = IdResolutionMode::DIRECT_OFFSET;
    } else {
        return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE,
                         "Instruction ID has an unsupported encoded width");
    }
    const auto kind = RecognizeIdKind(slot);
    if (kind == RecognizedIdKind::UNKNOWN) {
        operand.kind = IdOperandKind::UNKNOWN;
        return {DisassemblyStatus::SUCCESS, std::move(operand), {}};
    }
    if (kind == RecognizedIdKind::CONFLICT) {
        return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE,
                         "Instruction ID has conflicting generated type flags");
    }
    uint32_t resolvedOffset = 0U;
    std::string diagnostic;
    if (operand.resolutionMode == IdResolutionMode::INDEX) {
        if (!ResolveCurrentMethodIndex(*context.file, *context.currentIndex, rawValue, &resolvedOffset, &diagnostic)) {
            return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE, std::move(diagnostic));
        }
    } else {
        resolvedOffset = rawValue;
        if (!panda_file::File::EntityId(resolvedOffset).IsValid() ||
            resolvedOffset >= context.file->GetHeader()->file_size) {
            return IdFailure(context, DisassemblyStatus::MALFORMED_BYTECODE,
                             "Instruction direct ID offset resolves outside the ABC file");
        }
    }
    operand.resolvedOffset = resolvedOffset;
    if (kind == RecognizedIdKind::STRING) {
        return ResolveStringIdOperand(context, std::move(operand), slot);
    }
    if (kind == RecognizedIdKind::METHOD) {
        return ResolveMethodIdOperand(context, std::move(operand), slot);
    }
    return ResolveLiteralArrayIdOperand(context, std::move(operand));
}

ModelString GetInstructionMnemonic(const BytecodeInstructionSafe &instruction, pmr::memory_resource *resource)
{
    constexpr uint64_t maxGeneratedInstructionText = 1024U;
    BoundedStringBuffer buffer(maxGeneratedInstructionText, resource);
    std::ostream output(&buffer);
    output << instruction;
    if (buffer.Exceeded() || !output) {
        return {};
    }
    auto mnemonic = buffer.TakeValue();
    const auto separator = mnemonic.find(' ');
    if (separator != std::string::npos) {
        mnemonic.erase(separator);
    }
    return mnemonic;
}

ControlFlowKind GetControlFlowKind(const BytecodeInstructionSafe &instruction)
{
    if (instruction.HasFlag(BytecodeInstructionSafe::Flags::RETURN)) {
        return ControlFlowKind::RETURN;
    }
    if (instruction.IsThrow(BytecodeInstructionSafe::Exceptions::X_THROW)) {
        return ControlFlowKind::THROW;
    }
    if (instruction.IsJumpInstruction()) {
        return instruction.HasFlag(BytecodeInstructionSafe::Flags::CONDITIONAL) ? ControlFlowKind::CONDITIONAL_JUMP
                                                                                : ControlFlowKind::UNCONDITIONAL_JUMP;
    }
    return ControlFlowKind::NORMAL;
}

// The three operand decoders below are the extracted case bodies of the operand switch in
// DecodeInstructions: each validates one operand kind, appends it to the instruction info and
// formats its text, preserving the original check order.

DisassemblyResult DecodeRegisterOperand(const OperandSlot &slot, const CodeLayout &layout, InstructionInfo *info,
                                        ModelString *formattedOperand)
{
    if (slot.descriptor->typedIndex != info->registers.size()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Register operand descriptor order is malformed");
    }
    const auto reg = slot.instruction->GetVReg(slot.descriptor->typedIndex);
    if (!slot.instruction->IsValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Register operand crosses the code range");
    }
    info->registers.push_back(reg);
    if (!DisassemblyFormatter::FormatRegister(reg, layout.vregCount, slot.textRemaining, formattedOperand)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Formatted register exceeds the configured limit");
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

DisassemblyResult DecodeImmediateOperand(const OperandSlot &slot, InstructionInfo *info, ModelString *formattedOperand)
{
    if (slot.descriptor->typedIndex != info->immediates.size()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Immediate operand descriptor order is malformed");
    }
    const auto type = slot.instruction->GetImmType(slot.descriptor->typedIndex);
    const auto rawBits = slot.instruction->GetImmRaw(slot.descriptor->typedIndex);
    if (!slot.instruction->IsValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Immediate operand crosses the code range");
    }
    ImmediateValue immediate;
    immediate.bitWidth = type.bitWidth;
    immediate.rawBits = rawBits;
    switch (type.kind) {
        case BytecodeImmediateKind::SIGNED:
            immediate.kind = ImmediateKind::SIGNED;
            immediate.value = slot.instruction->GetImmData(slot.descriptor->typedIndex);
            break;
        case BytecodeImmediateKind::UNSIGNED:
            immediate.kind = ImmediateKind::UNSIGNED;
            immediate.value = rawBits;
            break;
        case BytecodeImmediateKind::FLOAT:
            immediate.kind = ImmediateKind::FLOAT;
            if (type.bitWidth == 32U) {
                immediate.value = static_cast<double>(bit_cast<float>(static_cast<uint32_t>(rawBits)));
            } else if (type.bitWidth == 64U) {
                immediate.value = bit_cast<double>(rawBits);
            } else {
                return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                               "Floating immediate has an unsupported encoded width");
            }
            break;
    }
    if (!slot.instruction->IsValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Immediate operand crosses the code range");
    }
    if (!DisassemblyFormatter::FormatImmediateOperand(immediate, slot.textRemaining, formattedOperand)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Formatted immediate exceeds the configured limit");
    }
    info->immediates.push_back(std::move(immediate));
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

DisassemblyResult DecodeIdOperand(const DecodeContext &context, const OperandSlot &slot, InstructionInfo *info,
                                  ModelString *formattedOperand)
{
    if (slot.descriptor->typedIndex != info->ids.size()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "ID operand descriptor order is malformed");
    }
    const auto rawValue = slot.instruction->GetId(slot.descriptor->typedIndex).AsRawValue();
    if (!slot.instruction->IsValid()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "ID operand crosses the code range");
    }
    auto resolved = ResolveIdOperand(context, slot, rawValue);
    if (resolved.status != DisassemblyStatus::SUCCESS) {
        return Failure(resolved.status, std::move(resolved.diagnostic));
    }
    const uint64_t idTextRemaining = slot.textRemaining - std::min<uint64_t>(slot.operandsBytes, slot.textRemaining);
    if (!DisassemblyFormatter::FormatIdOperand(resolved.operand, idTextRemaining, formattedOperand)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Formatted ID text exceeds the configured limit");
    }
    info->ids.push_back(std::move(resolved.operand));
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

DisassemblyResult DecodeOperand(const DecodeContext &context, const OperandSlot &slot, const CodeLayout &layout,
                                InstructionInfo *info, ModelString *formattedOperand)
{
    switch (slot.descriptor->kind) {
        case BytecodeOperandKind::REGISTER:
            return DecodeRegisterOperand(slot, layout, info, formattedOperand);
        case BytecodeOperandKind::IMMEDIATE:
            return DecodeImmediateOperand(slot, info, formattedOperand);
        case BytecodeOperandKind::ID:
            return DecodeIdOperand(context, slot, info, formattedOperand);
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Counts the instructions of one code range, enforcing the instruction-count limit and
// the exact code-boundary termination before any model allocation happens.
DisassemblyResult CountInstructions(const DecodeContext &context, const uint8_t *instructions, const CodeLayout &layout,
                                    const uint8_t *lastByte, uint64_t *instructionCount)
{
    uint32_t pc = 0U;
    uint64_t count = 0U;
    while (pc < layout.codeSize) {
        if (context.cancellation->IsCancellationRequested()) {
            return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
        }
        BytecodeInstructionSafe instruction(instructions + pc, instructions, lastByte);  // NOLINT
        const auto valid = ValidateInstruction(instruction, pc, layout.codeSize - pc);
        if (valid.status != DisassemblyStatus::SUCCESS) {
            return valid;
        }
        ++count;
        if (count > context.limits->maxInstructionCount) {
            return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Instruction count exceeds the configured limit");
        }
        pc += static_cast<uint32_t>(instruction.GetSize());
    }
    if (pc != layout.codeSize) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Instruction stream does not end at the code boundary");
    }
    *instructionCount = count;
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Per-kind operand counts of one instruction, used to reserve the model vectors.
struct OperandKindCounts {
    size_t registers {0U};
    size_t immediates {0U};
    size_t ids {0U};
};

OperandKindCounts CountOperandKinds(const BytecodeInstructionSafe &instruction)
{
    OperandKindCounts counts;
    const auto operandCount = instruction.GetOperandCount();
    for (size_t operandIndex = 0U; operandIndex < operandCount; ++operandIndex) {
        const auto descriptor = instruction.GetOperandDescriptor(operandIndex);
        switch (descriptor.kind) {
            case BytecodeOperandKind::REGISTER:
                ++counts.registers;
                break;
            case BytecodeOperandKind::IMMEDIATE:
                ++counts.immediates;
                break;
            case BytecodeOperandKind::ID:
                ++counts.ids;
                break;
        }
    }
    return counts;
}

// Formats the operands of one instruction into info->operands while advancing the
// formatted-text budget, preserving the original separator and limit checks.
DisassemblyResult FormatInstructionOperands(const DecodeContext &context, const BytecodeInstructionSafe &instruction,
                                            const CodeLayout &layout, InstructionInfo *info, uint64_t *formattedBytes)
{
    const auto operandCount = instruction.GetOperandCount();
    const auto counts = CountOperandKinds(instruction);
    info->registers.reserve(counts.registers);
    info->immediates.reserve(counts.immediates);
    info->ids.reserve(counts.ids);
    uint64_t fixedTextBytes = info->mnemonic.size();
    if (operandCount != 0U && !TryAdd(fixedTextBytes, 1U, &fixedTextBytes)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                       "Formatted instruction text exceeds the configured limit");
    }
    if (!AddWithinLimit(fixedTextBytes, context.limits->maxFormattedTextBytes, formattedBytes)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                       "Formatted instruction text exceeds the configured limit");
    }
    const uint64_t textRemaining =
        context.limits->maxFormattedTextBytes - std::min(*formattedBytes, context.limits->maxFormattedTextBytes);
    BoundedStringBuffer operandsBuffer(textRemaining, context.resource);
    std::ostream operandsOutput(&operandsBuffer);

    for (size_t operandIndex = 0U; operandIndex < operandCount; ++operandIndex) {
        const bool writesSeparator = operandIndex != 0U;
        if (writesSeparator) {
            operandsOutput << ", ";
        }
        if (writesSeparator && (operandsBuffer.Exceeded() || !operandsOutput)) {
            return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Formatted instruction text exceeds the configured limit");
        }
        const auto descriptor = instruction.GetOperandDescriptor(operandIndex);
        ModelString formattedOperand(context.resource);
        const OperandSlot slot {&instruction, &descriptor, textRemaining, operandsBuffer.Value().size()};
        const auto decoded = DecodeOperand(context, slot, layout, info, &formattedOperand);
        if (decoded.status != DisassemblyStatus::SUCCESS) {
            return decoded;
        }
        operandsOutput << formattedOperand;
        if (operandsBuffer.Exceeded() || !operandsOutput) {
            return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Formatted instruction text exceeds the configured limit");
        }
    }
    info->operands = operandsBuffer.TakeValue();
    if (!AddWithinLimit(info->operands.size(), context.limits->maxFormattedTextBytes, formattedBytes)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED,
                       "Formatted instruction text exceeds the configured limit");
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Resolves the jump target of a jump instruction into info->jumpTarget.
DisassemblyResult ResolveJumpTarget(const BytecodeInstructionSafe &instruction, uint32_t pc, const CodeLayout &layout,
                                    InstructionInfo *info)
{
    if (!instruction.IsJumpInstruction()) {
        return {DisassemblyStatus::SUCCESS, nullptr, {}};
    }
    if (info->immediates.empty()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Jump instruction has no immediate operand");
    }
    const auto &displacement = info->immediates.front();
    if (displacement.kind != ImmediateKind::SIGNED || !std::holds_alternative<int64_t>(displacement.value)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       "Jump instruction immediate is not a signed displacement");
    }
    int64_t target = 0;
    if (!TryAddPcRelative(pc, std::get<int64_t>(displacement.value), &target) || target < 0 ||
        target >= static_cast<int64_t>(layout.codeSize)) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Jump target is outside the code range");
    }
    info->jumpTarget = static_cast<uint32_t>(target);
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Post-decode boundary validation: exception-table offsets and jump targets must land
// on instruction boundaries.
DisassemblyResult ValidateInstructionBoundaries(const DecodeContext &context, const CodeLayout &layout,
                                                const ModelVector<uint32_t> &instructionOffsets,
                                                const ModelVector<InstructionInfo> &decoded)
{
    for (const auto boundary : layout.exceptionBoundaries) {
        if (context.cancellation->IsCancellationRequested()) {
            return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
        }
        if (boundary != layout.codeSize &&
            !std::binary_search(instructionOffsets.begin(), instructionOffsets.end(), boundary)) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                           "Exception table offset is not an instruction boundary");
        }
    }
    for (const auto &instruction : decoded) {
        if (context.cancellation->IsCancellationRequested()) {
            return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
        }
        if (instruction.jumpTarget.has_value() &&
            !std::binary_search(instructionOffsets.begin(), instructionOffsets.end(), *instruction.jumpTarget)) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Jump target is not an instruction boundary");
        }
    }
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Per-instruction decode outputs: the running formatted-text budget, the decoded byte size
// reported back for cursor advancement and the instruction model under construction.
struct DecodeOutputs {
    uint64_t *formattedBytes {nullptr};
    uint32_t *instructionSize {nullptr};
    InstructionInfo *info {nullptr};
};

// Decodes the instruction at pc into the output slots: validation, model fields, operand
// formatting and jump-target resolution. The byte size is returned through the outputs so
// the caller can advance pc.
DisassemblyResult DecodeSingleInstruction(const DecodeContext &context, const uint8_t *instructions,
                                          const CodeLayout &layout, uint32_t pc, const DecodeOutputs &outputs)
{
    const auto *lastByte = instructions + layout.codeSize - 1U;                      // NOLINT
    BytecodeInstructionSafe instruction(instructions + pc, instructions, lastByte);  // NOLINT
    const auto valid = ValidateInstruction(instruction, pc, layout.codeSize - pc);
    if (valid.status != DisassemblyStatus::SUCCESS) {
        return valid;
    }
    const auto size = instruction.GetSize();
    *outputs.instructionSize = static_cast<uint32_t>(size);

    auto *info = outputs.info;
    info->pc = pc;
    info->byteSize = static_cast<uint32_t>(size);
    info->numericOpcode = static_cast<uint16_t>(instruction.GetOpcode());
    info->mnemonic = GetInstructionMnemonic(instruction, context.resource);
    if (info->mnemonic.empty()) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "Instruction has no mnemonic");
    }
    info->bytes.assign(instructions + pc, instructions + pc + size);  // NOLINT

    const auto formatted = FormatInstructionOperands(context, instruction, layout, info, outputs.formattedBytes);
    if (formatted.status != DisassemblyStatus::SUCCESS) {
        return formatted;
    }
    const auto jump = ResolveJumpTarget(instruction, pc, layout, info);
    if (jump.status != DisassemblyStatus::SUCCESS) {
        return jump;
    }
    info->fileRange = {layout.instructionsOffset + pc, size};
    info->flowKind = GetControlFlowKind(instruction);
    // Dynamic conditional throws carry a property even when their ISA exception list is x_none.
    // They keep normal fallthrough, but protected blocks must also retain their catch edge.
    info->canThrow = instruction.CanThrow() || instruction.HasFlag(BytecodeInstructionSafe::Flags::CONDITIONAL_THROW);
    info->isTerminator = instruction.IsTerminator();
    return {DisassemblyStatus::SUCCESS, nullptr, {}};
}

// Decodes a whole method instruction stream into the result model: a counting pass
// first, then the per-instruction decode pass and the boundary validation.
DisassemblyResult DecodeInstructions(const DecodeContext &context, uint64_t methodOffset, CodeLayout layout,
                                     const uint8_t *instructions, std::shared_ptr<FunctionDisassembly> result)
{
    auto *resource = context.resource;
    const auto *lastByte = instructions + layout.codeSize - 1U;  // NOLINT
    uint64_t instructionCount = 0U;
    const auto counted = CountInstructions(context, instructions, layout, lastByte, &instructionCount);
    if (counted.status != DisassemblyStatus::SUCCESS) {
        return counted;
    }

    ModelVector<InstructionInfo> decoded(resource);
    ModelVector<uint32_t> instructionOffsets(resource);
    if (instructionCount > static_cast<uint64_t>(decoded.max_size()) ||
        instructionCount > static_cast<uint64_t>(instructionOffsets.max_size())) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Instruction count exceeds host vector capacity");
    }

    decoded.reserve(static_cast<size_t>(instructionCount));
    instructionOffsets.reserve(static_cast<size_t>(instructionCount));

    uint64_t formattedBytes = 0U;
    uint32_t pc = 0U;
    while (pc < layout.codeSize) {
        if (context.cancellation->IsCancellationRequested()) {
            return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
        }
        InstructionInfo info(resource);
        uint32_t instructionSize = 0U;
        const auto decodedInstruction =
            DecodeSingleInstruction(context, instructions, layout, pc, {&formattedBytes, &instructionSize, &info});
        if (decodedInstruction.status != DisassemblyStatus::SUCCESS) {
            return decodedInstruction;
        }
        instructionOffsets.push_back(pc);
        decoded.push_back(std::move(info));
        pc += instructionSize;
    }

    const auto boundaries = ValidateInstructionBoundaries(context, layout, instructionOffsets, decoded);
    if (boundaries.status != DisassemblyStatus::SUCCESS) {
        return boundaries;
    }

    result->methodOffset = methodOffset;
    result->codeOffset = layout.codeOffset;
    result->instructionsOffset = layout.instructionsOffset;
    result->codeSize = layout.codeSize;
    result->vregCount = layout.vregCount;
    result->argumentCount = layout.argumentCount;
    result->tryRegions = std::move(layout.tryRegions);
    result->instructions = std::move(decoded);
    return {DisassemblyStatus::SUCCESS, std::move(result), {}};
}

}  // namespace

DisassemblyResult FunctionDisassembler::Disassemble(const panda_file::File &file, const MethodIndex &methods,
                                                    const ExecutableTarget &target, const DisassemblyLimits &limits,
                                                    const CancellationToken &cancellation) const
{
    if (cancellation.IsCancellationRequested()) {
        return Failure(DisassemblyStatus::CANCELLED, "Disassembly was cancelled");
    }
    const auto *method = methods.Find(target.methodOffset);
    if (method == nullptr) {
        return Failure(DisassemblyStatus::INVALID_METHOD, "Offset does not identify a METHOD entity in this file");
    }
    if (!method->codeOffset.has_value()) {
        return Failure(DisassemblyStatus::NO_CODE, "METHOD has no code item");
    }
    if (limits.maxModelBytes < sizeof(FunctionDisassembly)) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Disassembly model exceeds the configured limit");
    }

    auto resource = std::make_shared<BoundedMemoryResource>(limits.maxModelBytes - sizeof(FunctionDisassembly));
    auto result = std::make_shared<FunctionDisassembly>(resource);
    try {
        auto layout = ReadCodeLayout(file, *method->codeOffset, limits, cancellation, resource.get());
        if (layout.status != DisassemblyStatus::SUCCESS) {
            return Failure(layout.status, layout.diagnostic);
        }
        const auto currentIndex = ReadCurrentMethodIndex(file, method->methodOffset, cancellation);
        if (currentIndex.status != DisassemblyStatus::SUCCESS) {
            return Failure(currentIndex.status, currentIndex.diagnostic);
        }
        auto literalContext = BuildLiteralValidationContext(file, cancellation, resource.get());
        if (literalContext.status != DisassemblyStatus::SUCCESS) {
            return Failure(literalContext.status, literalContext.diagnostic);
        }

        panda_file::MethodDataAccessor methodAccessor(
            file, panda_file::File::EntityId(static_cast<uint32_t>(method->methodOffset)));
        const auto codeId = methodAccessor.GetCodeId();
        if (!codeId.has_value() || codeId->GetOffset() != *method->codeOffset) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "METHOD code metadata is inconsistent");
        }
        panda_file::CodeDataAccessor code(file, *codeId);
        const auto instructionsOffset = file.GetIdFromPointer(code.GetInstructions()).GetOffset();
        if (code.GetNumVregs() != layout.layout.vregCount || code.GetNumArgs() != layout.layout.argumentCount ||
            code.GetCodeSize() != layout.layout.codeSize || code.GetTriesSize() != layout.layout.tryCount ||
            instructionsOffset != layout.layout.instructionsOffset) {
            return Failure(DisassemblyStatus::MALFORMED_BYTECODE, "METHOD code metadata is inconsistent");
        }
        const DecodeContext decodeContext {&file,   &methods,      &currentIndex.index, &literalContext.context,
                                           &limits, &cancellation, result->Resource()};
        return DecodeInstructions(decodeContext, method->methodOffset, std::move(layout.layout), code.GetInstructions(),
                                  std::move(result));
    } catch (const ModelBudgetExceeded &) {
        return Failure(DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, "Disassembly model exceeds the configured limit");
    }
}

DisassemblyResult FunctionDisassembler::Disassemble(const panda_file::File &file, const ExecutableTarget &target,
                                                    const DisassemblyLimits &limits,
                                                    const CancellationToken &cancellation) const
{
    const auto built = MethodIndex::Build(file, cancellation);
    if (built.status == MethodIndexStatus::CANCELLED) {
        return Failure(DisassemblyStatus::CANCELLED, built.diagnostic);
    }
    if (built.status != MethodIndexStatus::SUCCESS || built.index == nullptr) {
        return Failure(DisassemblyStatus::MALFORMED_BYTECODE,
                       built.diagnostic.empty() ? "File structure is malformed while indexing METHOD entities"
                                                : built.diagnostic);
    }
    return Disassemble(file, *built.index, target, limits, cancellation);
}

uint64_t FunctionDisassembler::EstimateModelBytes(const FunctionDisassembly &disassembly)
{
    if (disassembly.OwnedResource() != nullptr) {
        return SaturatingAdd(sizeof(FunctionDisassembly), disassembly.OwnedResource()->LiveBytes());
    }
    uint64_t bytes = sizeof(FunctionDisassembly);
    bytes = SaturatingAdd(bytes, SaturatingMultiply(disassembly.tryRegions.capacity(), sizeof(TryRegionInfo)));
    for (const auto &tryRegion : disassembly.tryRegions) {
        bytes = SaturatingAdd(bytes, SaturatingMultiply(tryRegion.handlers.capacity(), sizeof(CatchHandlerInfo)));
    }
    bytes = SaturatingAdd(bytes, SaturatingMultiply(disassembly.instructions.capacity(), sizeof(InstructionInfo)));
    for (const auto &instruction : disassembly.instructions) {
        bytes = SaturatingAdd(bytes, instruction.mnemonic.capacity());
        bytes = SaturatingAdd(bytes, instruction.operands.capacity());
        bytes = SaturatingAdd(bytes, instruction.bytes.capacity());
        bytes = SaturatingAdd(bytes, SaturatingMultiply(instruction.registers.capacity(), sizeof(uint16_t)));
        bytes = SaturatingAdd(bytes, SaturatingMultiply(instruction.immediates.capacity(), sizeof(ImmediateValue)));
        bytes = SaturatingAdd(bytes, SaturatingMultiply(instruction.ids.capacity(), sizeof(IdOperand)));
        for (const auto &id : instruction.ids) {
            bytes = SaturatingAdd(bytes, id.resolvedText.capacity());
        }
    }
    return bytes;
}

uint64_t FunctionDisassembler::EstimatePeakModelBytes(const FunctionDisassembly &disassembly)
{
    if (disassembly.OwnedResource() != nullptr) {
        return SaturatingAdd(sizeof(FunctionDisassembly), disassembly.OwnedResource()->PeakBytes());
    }
    return EstimateModelBytes(disassembly);
}

}  // namespace panda::bytecode_explorer
