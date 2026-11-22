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

#ifndef BYTECODE_EXPLORER_CORE_CFG_CONTROL_FLOW_GRAPH_H
#define BYTECODE_EXPLORER_CORE_CFG_CONTROL_FLOW_GRAPH_H

#include "core/common/bounded_memory_resource.h"
#include "core/disassembly.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace panda::bytecode_explorer {

using BasicBlockId = uint32_t;
constexpr BasicBlockId INVALID_BASIC_BLOCK_ID = std::numeric_limits<BasicBlockId>::max();

enum class CfgEdgeKind { FALLTHROUGH, TRUE_BRANCH, FALSE_BRANCH, JUMP, EXCEPTION, RETURN, THROW };

struct BasicBlock {
    BasicBlockId id {INVALID_BASIC_BLOCK_ID};
    uint32_t startPc {0U};
    uint32_t endPc {0U};
    uint32_t firstInstructionIndex {0U};
    uint32_t instructionCount {0U};
    bool isEntry {false};
    bool isHandler {false};
    bool hasThrowingInstruction {false};
};

struct ControlFlowEdge {
    uint32_t id {0U};
    BasicBlockId source {INVALID_BASIC_BLOCK_ID};
    std::optional<BasicBlockId> target;
    CfgEdgeKind kind {CfgEdgeKind::FALLTHROUGH};
    std::optional<uint32_t> tryRegionIndex;
    std::optional<uint32_t> handlerIndex;
    std::optional<uint32_t> catchTypeIndex;
};

struct ControlFlowGraph {
private:
    // Declared before every PMR field so it is destroyed after them.
    std::shared_ptr<BoundedMemoryResource> ownedResource_;

public:
    ControlFlowGraph(std::shared_ptr<BoundedMemoryResource> resource, FunctionDisassemblyPtr source)
        : ownedResource_(std::move(resource)),
          disassembly(std::move(source)),
          blocks(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource()),
          edges(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource())
    {
    }

    ControlFlowGraph(const ControlFlowGraph &) = delete;
    ControlFlowGraph &operator=(const ControlFlowGraph &) = delete;
    ControlFlowGraph(ControlFlowGraph &&) = delete;
    ControlFlowGraph &operator=(ControlFlowGraph &&) = delete;
    ~ControlFlowGraph() = default;

    BoundedMemoryResource *OwnedResource() const
    {
        return ownedResource_.get();
    }

    FunctionDisassemblyPtr disassembly;
    BasicBlockId entryBlock {INVALID_BASIC_BLOCK_ID};
    ModelVector<BasicBlock> blocks;
    ModelVector<ControlFlowEdge> edges;
};

using ControlFlowGraphPtr = std::shared_ptr<const ControlFlowGraph>;

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_CFG_CONTROL_FLOW_GRAPH_H
