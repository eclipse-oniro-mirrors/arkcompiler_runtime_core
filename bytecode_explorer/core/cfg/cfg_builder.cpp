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

#include "core/cfg/cfg_builder.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

CfgValidationResult Valid()
{
    return {CfgStatus::SUCCESS, {}};
}

CfgValidationResult Invalid(std::string diagnostic)
{
    return {CfgStatus::INVALID_DISASSEMBLY, std::move(diagnostic)};
}

CfgBuildResult Failure(CfgStatus status, std::string diagnostic)
{
    return {status, nullptr, std::move(diagnostic)};
}

bool IsTerminalEdge(CfgEdgeKind kind)
{
    return kind == CfgEdgeKind::RETURN || kind == CfgEdgeKind::THROW;
}

bool IsInstructionBoundary(const FunctionDisassembly &disassembly, uint32_t pc, bool allowCodeEnd)
{
    if (allowCodeEnd && pc == disassembly.codeSize) {
        return true;
    }
    const auto found =
        std::lower_bound(disassembly.instructions.begin(), disassembly.instructions.end(), pc,
                         [](const InstructionInfo &instruction, uint32_t value) { return instruction.pc < value; });
    return found != disassembly.instructions.end() && found->pc == pc;
}

std::optional<BasicBlockId> FindBlock(const ControlFlowGraph &graph, uint32_t startPc)
{
    const auto found = std::lower_bound(graph.blocks.begin(), graph.blocks.end(), startPc,
                                        [](const BasicBlock &block, uint32_t value) { return block.startPc < value; });
    if (found == graph.blocks.end() || found->startPc != startPc) {
        return std::nullopt;
    }
    return found->id;
}

// Checks that the instruction stream is contiguous, bounded by the code size and
// self-consistent in its control-flow metadata.
CfgValidationResult ValidateInstructionSequence(const FunctionDisassembly &disassembly,
                                                const CancellationToken &cancellation)
{
    uint32_t expectedPc = 0U;
    for (const auto &instruction : disassembly.instructions) {
        if (cancellation.IsCancellationRequested()) {
            return {CfgStatus::CANCELLED, "CFG validation was cancelled"};
        }
        if (instruction.pc != expectedPc || instruction.byteSize == 0U ||
            instruction.byteSize > disassembly.codeSize - std::min(expectedPc, disassembly.codeSize)) {
            return Invalid("Disassembly instructions must be contiguous and bounded by code size");
        }
        const bool isControlTransfer = instruction.flowKind != ControlFlowKind::NORMAL;
        if (instruction.isTerminator != isControlTransfer) {
            return Invalid("Instruction terminator metadata disagrees with its control-flow kind");
        }
        const bool isJump = instruction.flowKind == ControlFlowKind::CONDITIONAL_JUMP ||
                            instruction.flowKind == ControlFlowKind::UNCONDITIONAL_JUMP;
        if (isJump != instruction.jumpTarget.has_value()) {
            return Invalid("Jump target metadata disagrees with the instruction control-flow kind");
        }
        if (instruction.flowKind == ControlFlowKind::CONDITIONAL_JUMP &&
            instruction.byteSize == disassembly.codeSize - expectedPc) {
            return Invalid("Conditional jump has no in-range fallthrough instruction");
        }
        expectedPc += instruction.byteSize;
    }
    if (expectedPc != disassembly.codeSize) {
        return Invalid("Disassembly instructions must be contiguous through the code boundary");
    }
    return Valid();
}

// Checks that every recorded jump target lands on an instruction boundary.
CfgValidationResult ValidateJumpTargets(const FunctionDisassembly &disassembly, const CancellationToken &cancellation)
{
    for (const auto &instruction : disassembly.instructions) {
        if (cancellation.IsCancellationRequested()) {
            return {CfgStatus::CANCELLED, "CFG validation was cancelled"};
        }
        if (instruction.jumpTarget.has_value() && !IsInstructionBoundary(disassembly, *instruction.jumpTarget, false)) {
            return Invalid("Jump target is not an instruction boundary");
        }
    }
    return Valid();
}

// Checks that try regions and their handlers stay inside the code and align to
// instruction boundaries.
CfgValidationResult ValidateTryRegionBoundaries(const FunctionDisassembly &disassembly,
                                                const CancellationToken &cancellation)
{
    const CfgValidationResult cancelled {CfgStatus::CANCELLED, "CFG validation was cancelled"};
    for (size_t tryIndex = 0U; tryIndex < disassembly.tryRegions.size(); ++tryIndex) {
        if (cancellation.IsCancellationRequested()) {
            return cancelled;
        }
        const auto &tryRegion = disassembly.tryRegions[tryIndex];
        if (tryRegion.startPc > tryRegion.endPc || !IsInstructionBoundary(disassembly, tryRegion.startPc, false) ||
            !IsInstructionBoundary(disassembly, tryRegion.endPc, true)) {
            return Invalid("Try region is not aligned to instruction boundaries");
        }
        for (size_t handlerIndex = 0U; handlerIndex < tryRegion.handlers.size(); ++handlerIndex) {
            if (cancellation.IsCancellationRequested()) {
                return cancelled;
            }
            const auto &handler = tryRegion.handlers[handlerIndex];
            if (handler.order != handlerIndex || !IsInstructionBoundary(disassembly, handler.handlerPc, false) ||
                handler.handlerSize > disassembly.codeSize - handler.handlerPc ||
                !IsInstructionBoundary(disassembly, handler.handlerPc + handler.handlerSize, true)) {
                return Invalid("Exception handler metadata is malformed or not instruction-aligned");
            }
        }
    }
    return Valid();
}

// Checks that blocks tile the instruction stream exactly, in pc order.
CfgValidationResult ValidateBlockCoverage(const ControlFlowGraph &graph, const CancellationToken &cancellation)
{
    uint32_t expectedPc = 0U;
    for (size_t index = 0U; index < graph.blocks.size(); ++index) {
        if (cancellation.IsCancellationRequested()) {
            return {CfgStatus::CANCELLED, "CFG validation was cancelled"};
        }
        const auto &block = graph.blocks[index];
        if (block.id != index || block.startPc != expectedPc || block.endPc <= block.startPc ||
            block.firstInstructionIndex >= graph.disassembly->instructions.size() || block.instructionCount == 0U ||
            block.instructionCount > graph.disassembly->instructions.size() - block.firstInstructionIndex) {
            return {CfgStatus::INTERNAL_ERROR, "CFG basic-block coverage is inconsistent"};
        }
        const auto &first = graph.disassembly->instructions[block.firstInstructionIndex];
        const auto &last = graph.disassembly->instructions[block.firstInstructionIndex + block.instructionCount - 1U];
        if (first.pc != block.startPc || last.pc + last.byteSize != block.endPc) {
            return {CfgStatus::INTERNAL_ERROR, "CFG block instruction range is inconsistent"};
        }
        expectedPc = block.endPc;
    }
    if (expectedPc != graph.disassembly->codeSize || !graph.blocks[graph.entryBlock].isEntry) {
        return {CfgStatus::INTERNAL_ERROR, "CFG blocks do not cover the complete instruction stream"};
    }
    return Valid();
}

// Checks edge ids, endpoints and exception-edge metadata consistency.
CfgValidationResult ValidateEdgeConsistency(const ControlFlowGraph &graph, const CancellationToken &cancellation)
{
    for (size_t index = 0U; index < graph.edges.size(); ++index) {
        if (cancellation.IsCancellationRequested()) {
            return {CfgStatus::CANCELLED, "CFG validation was cancelled"};
        }
        const auto &edge = graph.edges[index];
        if (edge.id != index || edge.source >= graph.blocks.size() ||
            (IsTerminalEdge(edge.kind) && edge.target.has_value()) ||
            (!IsTerminalEdge(edge.kind) && (!edge.target.has_value() || *edge.target >= graph.blocks.size()))) {
            return {CfgStatus::INTERNAL_ERROR, "CFG edge endpoint is inconsistent"};
        }
        const bool isException = edge.kind == CfgEdgeKind::EXCEPTION;
        if (isException != edge.tryRegionIndex.has_value() || isException != edge.handlerIndex.has_value() ||
            isException != edge.catchTypeIndex.has_value()) {
            return {CfgStatus::INTERNAL_ERROR, "CFG exception edge metadata is inconsistent"};
        }
    }
    return Valid();
}

}  // namespace

CfgValidationResult ControlFlowGraphValidator::ValidateDisassembly(const FunctionDisassembly &disassembly,
                                                                   const CancellationToken &cancellation)
{
    if (disassembly.codeSize == 0U || disassembly.instructions.empty()) {
        return Invalid("Disassembly must contain a non-empty instruction stream");
    }
    if (disassembly.instructions.size() > std::numeric_limits<uint32_t>::max()) {
        return Invalid("Instruction count exceeds the CFG index range");
    }
    const auto sequence = ValidateInstructionSequence(disassembly, cancellation);
    if (sequence.status != CfgStatus::SUCCESS) {
        return sequence;
    }
    const auto jumpTargets = ValidateJumpTargets(disassembly, cancellation);
    if (jumpTargets.status != CfgStatus::SUCCESS) {
        return jumpTargets;
    }
    return ValidateTryRegionBoundaries(disassembly, cancellation);
}

CfgValidationResult ControlFlowGraphValidator::ValidateGraph(const ControlFlowGraph &graph,
                                                             const CancellationToken &cancellation)
{
    if (graph.disassembly == nullptr || graph.blocks.empty() || graph.entryBlock >= graph.blocks.size()) {
        return {CfgStatus::INTERNAL_ERROR, "CFG has no valid source or entry block"};
    }
    const auto coverage = ValidateBlockCoverage(graph, cancellation);
    if (coverage.status != CfgStatus::SUCCESS) {
        return coverage;
    }
    return ValidateEdgeConsistency(graph, cancellation);
}

// Adds EXCEPTION edges from one throwing block to every handler of its try region.
// Returns false when the edge budget is exhausted.
template <typename AddEdge>
bool LinkThrowingBlockHandlers(const ControlFlowGraph &graph, const TryRegionInfo &tryRegion, size_t tryIndex,
                               BasicBlockId blockId, AddEdge addEdge)
{
    if (!graph.blocks[blockId].hasThrowingInstruction) {
        return true;
    }
    for (size_t handlerIndex = 0U; handlerIndex < tryRegion.handlers.size(); ++handlerIndex) {
        const auto &handler = tryRegion.handlers[handlerIndex];
        if (!addEdge(blockId, FindBlock(graph, handler.handlerPc), CfgEdgeKind::EXCEPTION,
                     static_cast<uint32_t>(tryIndex), static_cast<uint32_t>(handlerIndex), handler.typeIndex)) {
            return false;
        }
    }
    return true;
}

// Wires EXCEPTION edges from every throwing block inside one try region to its handler blocks.
// The returned result carries a failure when cancellation was requested or the edge budget is exhausted.
template <typename AddEdge>
CfgBuildResult LinkTryRegionHandlers(const ControlFlowGraph &graph, const TryRegionInfo &tryRegion, size_t tryIndex,
                                     AddEdge addEdge, const CancellationToken &cancellation)
{
    auto blockId = FindBlock(graph, tryRegion.startPc);
    while (blockId.has_value() && *blockId < graph.blocks.size() && graph.blocks[*blockId].startPc < tryRegion.endPc) {
        if (cancellation.IsCancellationRequested()) {
            return Failure(CfgStatus::CANCELLED, "CFG construction was cancelled");
        }
        if (!LinkThrowingBlockHandlers(graph, tryRegion, tryIndex, *blockId, addEdge)) {
            return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG edge count exceeds the configured limit");
        }
        ++*blockId;
    }
    return {CfgStatus::SUCCESS, nullptr, {}};
}

// Collects and deduplicates block leaders (jump targets, fallthrough starts, try and
// handler bounds) plus the handler entry starts. Fails when cancelled or when the block
// count would exceed its limit.
CfgBuildResult CollectLeaders(const FunctionDisassembly &disassembly, const CfgLimits &limits,
                              const CancellationToken &cancellation, std::vector<uint32_t> *leaders,
                              std::vector<uint32_t> *handlerStarts)
{
    leaders->reserve(disassembly.instructions.size() + 1U);
    leaders->push_back(0U);
    leaders->push_back(disassembly.codeSize);
    for (const auto &instruction : disassembly.instructions) {
        if (cancellation.IsCancellationRequested()) {
            return Failure(CfgStatus::CANCELLED, "CFG construction was cancelled");
        }
        if (instruction.jumpTarget.has_value()) {
            leaders->push_back(*instruction.jumpTarget);
        }
        const uint32_t nextPc = instruction.pc + instruction.byteSize;
        if (instruction.flowKind != ControlFlowKind::NORMAL && nextPc < disassembly.codeSize) {
            leaders->push_back(nextPc);
        }
    }
    for (const auto &tryRegion : disassembly.tryRegions) {
        if (tryRegion.startPc < disassembly.codeSize) {
            leaders->push_back(tryRegion.startPc);
        }
        if (tryRegion.endPc < disassembly.codeSize) {
            leaders->push_back(tryRegion.endPc);
        }
        for (const auto &handler : tryRegion.handlers) {
            leaders->push_back(handler.handlerPc);
            const uint32_t handlerEndPc = handler.handlerPc + handler.handlerSize;
            if (handlerEndPc < disassembly.codeSize) {
                leaders->push_back(handlerEndPc);
            }
            handlerStarts->push_back(handler.handlerPc);
        }
    }
    std::sort(leaders->begin(), leaders->end());
    leaders->erase(std::unique(leaders->begin(), leaders->end()), leaders->end());
    std::sort(handlerStarts->begin(), handlerStarts->end());
    handlerStarts->erase(std::unique(handlerStarts->begin(), handlerStarts->end()), handlerStarts->end());
    const uint64_t blockCount = leaders->size() - 1U;
    if (blockCount > limits.maxBlockCount || blockCount > std::numeric_limits<BasicBlockId>::max()) {
        return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG block count exceeds the configured limit");
    }
    return {CfgStatus::SUCCESS, nullptr, {}};
}

// Allocates the bounded CFG model and fills one block per leader interval.
CfgBuildResult CreateBlocks(const FunctionDisassemblyPtr &disassembly, const CfgLimits &limits,
                            const std::vector<uint32_t> &leaders, const std::vector<uint32_t> &handlerStarts,
                            std::shared_ptr<ControlFlowGraph> *graph)
{
    auto resource = std::make_shared<BoundedMemoryResource>(limits.maxModelBytes - sizeof(ControlFlowGraph));
    *graph = std::make_shared<ControlFlowGraph>(resource, disassembly);
    (*graph)->blocks.reserve(static_cast<size_t>(leaders.size() - 1U));
    size_t instructionIndex = 0U;
    for (size_t leaderIndex = 0U; leaderIndex + 1U < leaders.size(); ++leaderIndex) {
        const uint32_t startPc = leaders[leaderIndex];
        const uint32_t endPc = leaders[leaderIndex + 1U];
        const size_t firstInstructionIndex = instructionIndex;
        bool hasThrowingInstruction = false;
        while (instructionIndex < disassembly->instructions.size() &&
               disassembly->instructions[instructionIndex].pc < endPc) {
            hasThrowingInstruction = hasThrowingInstruction || disassembly->instructions[instructionIndex].canThrow;
            ++instructionIndex;
        }
        const size_t instructionCount = instructionIndex - firstInstructionIndex;
        (*graph)->blocks.push_back(
            {static_cast<BasicBlockId>(leaderIndex), startPc, endPc, static_cast<uint32_t>(firstInstructionIndex),
             static_cast<uint32_t>(instructionCount), leaderIndex == 0U,
             std::binary_search(handlerStarts.begin(), handlerStarts.end(), startPc), hasThrowingInstruction});
    }
    (*graph)->entryBlock = 0U;
    return {CfgStatus::SUCCESS, nullptr, {}};
}

// Appends the fallthrough/jump/return/throw exit edges of every block.
template <typename AddEdge>
CfgBuildResult AppendBlockExitEdges(const FunctionDisassembly &disassembly, const ControlFlowGraphPtr &graph,
                                    const CancellationToken &cancellation, AddEdge addEdge)
{
    for (const auto &block : graph->blocks) {
        if (cancellation.IsCancellationRequested()) {
            return Failure(CfgStatus::CANCELLED, "CFG construction was cancelled");
        }
        const auto &last = disassembly.instructions[block.firstInstructionIndex + block.instructionCount - 1U];
        const auto nextBlock =
            block.id + 1U < graph->blocks.size() ? std::optional<BasicBlockId> {block.id + 1U} : std::nullopt;
        bool added = true;
        switch (last.flowKind) {
            case ControlFlowKind::NORMAL:
                added = !nextBlock.has_value() || addEdge(block.id, nextBlock, CfgEdgeKind::FALLTHROUGH);
                break;
            case ControlFlowKind::CONDITIONAL_JUMP:
                added = addEdge(block.id, FindBlock(*graph, *last.jumpTarget), CfgEdgeKind::TRUE_BRANCH) &&
                        addEdge(block.id, nextBlock, CfgEdgeKind::FALSE_BRANCH);
                break;
            case ControlFlowKind::UNCONDITIONAL_JUMP:
                added = addEdge(block.id, FindBlock(*graph, *last.jumpTarget), CfgEdgeKind::JUMP);
                break;
            case ControlFlowKind::RETURN:
                added = addEdge(block.id, std::nullopt, CfgEdgeKind::RETURN);
                break;
            case ControlFlowKind::THROW:
                added = addEdge(block.id, std::nullopt, CfgEdgeKind::THROW);
                break;
        }
        if (!added) {
            return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG edge count exceeds the configured limit");
        }
    }
    return {CfgStatus::SUCCESS, nullptr, {}};
}

// Wires EXCEPTION edges for every non-empty try region of the disassembly.
template <typename AddEdge>
CfgBuildResult LinkTryRegions(const FunctionDisassembly &disassembly, const ControlFlowGraph &graph,
                              const CancellationToken &cancellation, AddEdge addEdge)
{
    for (size_t tryIndex = 0U; tryIndex < disassembly.tryRegions.size(); ++tryIndex) {
        const auto &tryRegion = disassembly.tryRegions[tryIndex];
        if (tryRegion.handlers.empty() || tryRegion.startPc == tryRegion.endPc) {
            continue;
        }
        const auto linked = LinkTryRegionHandlers(graph, tryRegion, tryIndex, addEdge, cancellation);
        if (linked.status != CfgStatus::SUCCESS) {
            return linked;
        }
    }
    return {CfgStatus::SUCCESS, nullptr, {}};
}

// Assembles blocks, exit edges and try regions into a validated graph model.
CfgBuildResult BuildValidatedGraph(const FunctionDisassemblyPtr &disassembly, const CfgLimits &limits,
                                   const CancellationToken &cancellation)
{
    try {
        std::vector<uint32_t> leaders;
        std::vector<uint32_t> handlerStarts;
        const auto collected = CollectLeaders(*disassembly, limits, cancellation, &leaders, &handlerStarts);
        if (collected.status != CfgStatus::SUCCESS) {
            return collected;
        }
        std::shared_ptr<ControlFlowGraph> model;
        const auto created = CreateBlocks(disassembly, limits, leaders, handlerStarts, &model);
        if (created.status != CfgStatus::SUCCESS) {
            return created;
        }
        const auto addEdge = [model, &limits](BasicBlockId source, std::optional<BasicBlockId> target, CfgEdgeKind kind,
                                              std::optional<uint32_t> tryRegionIndex = std::nullopt,
                                              std::optional<uint32_t> handlerIndex = std::nullopt,
                                              std::optional<uint32_t> catchTypeIndex = std::nullopt) {
            if (model->edges.size() >= limits.maxEdgeCount ||
                model->edges.size() >= std::numeric_limits<uint32_t>::max()) {
                return false;
            }
            model->edges.push_back({static_cast<uint32_t>(model->edges.size()), source, target, kind, tryRegionIndex,
                                    handlerIndex, catchTypeIndex});
            return true;
        };
        const auto exits = AppendBlockExitEdges(*disassembly, model, cancellation, addEdge);
        if (exits.status != CfgStatus::SUCCESS) {
            return exits;
        }
        const auto linked = LinkTryRegions(*disassembly, *model, cancellation, addEdge);
        if (linked.status != CfgStatus::SUCCESS) {
            return linked;
        }
        const auto graphValidation = ControlFlowGraphValidator::ValidateGraph(*model, cancellation);
        if (graphValidation.status != CfgStatus::SUCCESS) {
            return Failure(graphValidation.status, graphValidation.diagnostic);
        }
        return {CfgStatus::SUCCESS, std::move(model), {}};
    } catch (const ModelBudgetExceeded &) {
        return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG model exceeds the configured byte limit");
    } catch (const std::bad_alloc &) {
        return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG construction ran out of memory");
    }
}

CfgBuildResult ControlFlowGraphBuilder::Build(FunctionDisassemblyPtr disassembly, const CfgLimits &limits,
                                              const CancellationToken &cancellation) const
{
    if (disassembly == nullptr) {
        return Failure(CfgStatus::INVALID_DISASSEMBLY, "Cannot build a CFG without disassembly");
    }
    const auto sourceValidation = ControlFlowGraphValidator::ValidateDisassembly(*disassembly, cancellation);
    if (sourceValidation.status != CfgStatus::SUCCESS) {
        return Failure(sourceValidation.status, sourceValidation.diagnostic);
    }
    if (limits.maxModelBytes < sizeof(ControlFlowGraph)) {
        return Failure(CfgStatus::RESOURCE_LIMIT_EXCEEDED, "CFG model exceeds the configured byte limit");
    }
    return BuildValidatedGraph(disassembly, limits, cancellation);
}

}  // namespace panda::bytecode_explorer
