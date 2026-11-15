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

#ifndef BYTECODE_EXPLORER_CORE_DISASSEMBLY_H
#define BYTECODE_EXPLORER_CORE_DISASSEMBLY_H

#include "core/common/bounded_memory_resource.h"
#include "core/model/entity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace panda::bytecode_explorer {

using ModelString = pmr::string;

template <class T>
using ModelVector = pmr::vector<T>;

struct ExecutableTarget {
    EntityId entityId {};
    uint64_t methodOffset {};
    std::string methodName;
    std::optional<FileRange> codeRange;
};

enum class ImmediateKind { SIGNED, UNSIGNED, FLOAT };

struct ImmediateValue {
    ImmediateKind kind {ImmediateKind::SIGNED};
    size_t bitWidth {0U};
    uint64_t rawBits {0U};
    std::variant<int64_t, uint64_t, double> value {int64_t {0}};
};

enum class IdOperandKind { STRING, METHOD, LITERAL_ARRAY, UNKNOWN };

enum class IdResolutionMode { INDEX, DIRECT_OFFSET };

enum class ControlFlowKind { NORMAL, CONDITIONAL_JUMP, UNCONDITIONAL_JUMP, RETURN, THROW };

struct IdOperand {
    explicit IdOperand(pmr::memory_resource *resource = pmr::get_default_resource()) : resolvedText(resource)
    {
    }

    IdOperand(IdOperandKind operandKind, IdResolutionMode mode, uint32_t raw, std::optional<uint64_t> offset,
              std::string_view text, pmr::memory_resource *resource = pmr::get_default_resource())
        : kind(operandKind), resolutionMode(mode), rawValue(raw), resolvedOffset(offset), resolvedText(text, resource)
    {
    }

    IdOperandKind kind {IdOperandKind::UNKNOWN};
    IdResolutionMode resolutionMode {IdResolutionMode::INDEX};
    uint32_t rawValue {0U};
    std::optional<uint64_t> resolvedOffset;
    ModelString resolvedText;
};

struct InstructionInfo {
    explicit InstructionInfo(pmr::memory_resource *resource = pmr::get_default_resource())
        : mnemonic(resource),
          operands(resource),
          bytes(resource),
          registers(resource),
          immediates(resource),
          ids(resource)
    {
    }

    uint32_t pc {0U};
    uint32_t byteSize {0U};
    uint16_t numericOpcode {0U};
    ModelString mnemonic;
    ModelString operands;
    ModelVector<uint8_t> bytes;
    ModelVector<uint16_t> registers;
    ModelVector<ImmediateValue> immediates;
    ModelVector<IdOperand> ids;
    std::optional<uint32_t> jumpTarget;
    FileRange fileRange;
    ControlFlowKind flowKind {ControlFlowKind::NORMAL};
    bool canThrow {false};
    bool isTerminator {false};
};

struct CatchHandlerInfo {
    uint32_t typeIndex {0U};
    uint32_t handlerPc {0U};
    uint32_t handlerSize {0U};
    uint32_t order {0U};
};

struct TryRegionInfo {
    explicit TryRegionInfo(pmr::memory_resource *resource = pmr::get_default_resource()) : handlers(resource)
    {
    }

    uint32_t startPc {0U};
    uint32_t endPc {0U};
    ModelVector<CatchHandlerInfo> handlers;
};

struct FunctionDisassembly {
private:
    // Declared before every PMR field so it is destroyed after them.
    std::shared_ptr<BoundedMemoryResource> ownedResource_;

public:
    FunctionDisassembly() : tryRegions(pmr::get_default_resource()), instructions(pmr::get_default_resource())
    {
    }

    explicit FunctionDisassembly(std::shared_ptr<BoundedMemoryResource> resource)
        : ownedResource_(std::move(resource)),
          tryRegions(ownedResource_ != nullptr ? ownedResource_.get() : pmr::get_default_resource()),
          instructions(ownedResource_ != nullptr ? ownedResource_.get() : pmr::get_default_resource())
    {
    }

    FunctionDisassembly(const FunctionDisassembly &) = delete;
    FunctionDisassembly &operator=(const FunctionDisassembly &) = delete;
    FunctionDisassembly(FunctionDisassembly &&) = delete;
    FunctionDisassembly &operator=(FunctionDisassembly &&) = delete;
    ~FunctionDisassembly() = default;

    pmr::memory_resource *Resource() const
    {
        return instructions.get_allocator().resource();
    }

    BoundedMemoryResource *OwnedResource() const
    {
        return ownedResource_.get();
    }

    uint64_t methodOffset {0U};
    uint64_t codeOffset {0U};
    uint64_t instructionsOffset {0U};
    uint32_t codeSize {0U};
    uint32_t vregCount {0U};
    uint32_t argumentCount {0U};
    ModelVector<TryRegionInfo> tryRegions;
    ModelVector<InstructionInfo> instructions;
};

using FunctionDisassemblyPtr = std::shared_ptr<const FunctionDisassembly>;

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_DISASSEMBLY_H
