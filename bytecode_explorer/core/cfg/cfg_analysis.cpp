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

#include "core/cfg/cfg_analysis.h"

#include "core/cfg/cfg_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

struct Adjacency {
    std::vector<size_t> offsets;
    std::vector<BasicBlockId> targets;
};

struct DfsFrame {
    BasicBlockId block {INVALID_BASIC_BLOCK_ID};
    size_t nextEdge {0U};
};

// Inputs shared by the CFG analysis phases below: adjacency, reachability and cancellation,
// all derived once by CfgAnalyzer::Analyze.
struct CfgAnalysisContext {
    const Adjacency *successors {nullptr};
    const Adjacency *predecessors {nullptr};
    const std::vector<uint8_t> *reachable {nullptr};
    const CancellationToken *cancellation {nullptr};
};

// Mutable working state of one strongly connected component collection pass.
struct SccCollectionState {
    std::vector<uint8_t> assigned;
    std::vector<BasicBlockId> stack;
    uint64_t operations {0U};
};

CfgAnalysisResult Failure(CfgAnalysisStatus status, std::string diagnostic)
{
    return {status, nullptr, std::move(diagnostic)};
}

bool PollCancellation(const CancellationToken &cancellation, uint64_t *operations)
{
    constexpr uint64_t cancellationPollMask = 0x3ffU;
    ++*operations;
    return ((*operations & cancellationPollMask) == 0U) && cancellation.IsCancellationRequested();
}

std::pair<Adjacency, Adjacency> BuildAdjacency(const ControlFlowGraph &graph)
{
    const size_t blockCount = graph.blocks.size();
    Adjacency successors;
    Adjacency predecessors;
    successors.offsets.assign(blockCount + 1U, 0U);
    predecessors.offsets.assign(blockCount + 1U, 0U);
    size_t targetedEdgeCount = 0U;
    for (const auto &edge : graph.edges) {
        if (!edge.target.has_value()) {
            continue;
        }
        ++successors.offsets[edge.source + 1U];
        ++predecessors.offsets[*edge.target + 1U];
        ++targetedEdgeCount;
    }
    for (size_t index = 1U; index < successors.offsets.size(); ++index) {
        successors.offsets[index] += successors.offsets[index - 1U];
        predecessors.offsets[index] += predecessors.offsets[index - 1U];
    }
    successors.targets.resize(targetedEdgeCount);
    predecessors.targets.resize(targetedEdgeCount);
    auto successorCursor = successors.offsets;
    auto predecessorCursor = predecessors.offsets;
    for (const auto &edge : graph.edges) {
        if (!edge.target.has_value()) {
            continue;
        }
        successors.targets[successorCursor[edge.source]++] = *edge.target;
        predecessors.targets[predecessorCursor[*edge.target]++] = edge.source;
    }
    return {std::move(successors), std::move(predecessors)};
}

bool BuildReachablePostorder(BasicBlockId entry, const Adjacency &successors, const CancellationToken &cancellation,
                             std::vector<uint8_t> *reachable, std::vector<BasicBlockId> *postorder)
{
    uint64_t operations = 0U;
    std::vector<DfsFrame> stack;
    (*reachable)[entry] = 1U;
    stack.push_back({entry, successors.offsets[entry]});
    while (!stack.empty()) {
        auto &frame = stack.back();
        const size_t edgeEnd = successors.offsets[frame.block + 1U];
        if (frame.nextEdge < edgeEnd) {
            const auto target = successors.targets[frame.nextEdge++];
            if ((*reachable)[target] == 0U) {
                (*reachable)[target] = 1U;
                stack.push_back({target, successors.offsets[target]});
            }
        } else {
            postorder->push_back(frame.block);
            stack.pop_back();
        }
        if (PollCancellation(cancellation, &operations)) {
            return false;
        }
    }
    return !cancellation.IsCancellationRequested();
}

BasicBlockId IntersectDominators(BasicBlockId left, BasicBlockId right, const std::vector<BasicBlockId> &idom,
                                 const std::vector<uint32_t> &rpoPosition)
{
    while (left != right) {
        while (rpoPosition[left] > rpoPosition[right]) {
            left = idom[left];
        }
        while (rpoPosition[right] > rpoPosition[left]) {
            right = idom[right];
        }
    }
    return left;
}

bool Dominates(BasicBlockId dominator, BasicBlockId block, const std::vector<uint32_t> &domPreorder,
               const std::vector<uint32_t> &domPostorder)
{
    return domPreorder[dominator] <= domPreorder[block] && domPostorder[block] <= domPostorder[dominator];
}

Adjacency BuildDominatorTree(const std::vector<BasicBlockId> &rpo, const std::vector<BasicBlockId> &idom)
{
    Adjacency children;
    children.offsets.assign(idom.size() + 1U, 0U);
    for (size_t index = 1U; index < rpo.size(); ++index) {
        ++children.offsets[idom[rpo[index]] + 1U];
    }
    for (size_t index = 1U; index < children.offsets.size(); ++index) {
        children.offsets[index] += children.offsets[index - 1U];
    }
    children.targets.resize(rpo.size() - 1U);
    auto cursor = children.offsets;
    for (size_t index = 1U; index < rpo.size(); ++index) {
        const auto block = rpo[index];
        children.targets[cursor[idom[block]]++] = block;
    }
    return children;
}

bool NumberDominatorTree(BasicBlockId entry, const Adjacency &children, const CancellationToken &cancellation,
                         std::vector<uint32_t> *preorder, std::vector<uint32_t> *postorder)
{
    uint32_t clock = 0U;
    uint64_t operations = 0U;
    std::vector<DfsFrame> stack;
    (*preorder)[entry] = clock++;
    stack.push_back({entry, children.offsets[entry]});
    while (!stack.empty()) {
        auto &frame = stack.back();
        if (frame.nextEdge < children.offsets[frame.block + 1U]) {
            const auto child = children.targets[frame.nextEdge++];
            (*preorder)[child] = clock++;
            stack.push_back({child, children.offsets[child]});
        } else {
            (*postorder)[frame.block] = clock++;
            stack.pop_back();
        }
        if (PollCancellation(cancellation, &operations)) {
            return false;
        }
    }
    return !cancellation.IsCancellationRequested();
}

// Drains the worklist stack of one strongly connected component, appending reachable,
// still-unassigned predecessors. Returns false when cancellation was requested mid-collection.
bool CollectComponentMembers(const CfgAnalysisContext &context, SccCollectionState *state,
                             std::vector<BasicBlockId> *component)
{
    while (!state->stack.empty()) {
        const auto block = state->stack.back();
        state->stack.pop_back();
        component->push_back(block);
        for (size_t edge = context.predecessors->offsets[block]; edge < context.predecessors->offsets[block + 1U];
             ++edge) {
            const auto predecessor = context.predecessors->targets[edge];
            if ((*context.reachable)[predecessor] != 0U && state->assigned[predecessor] == 0U) {
                state->assigned[predecessor] = 1U;
                state->stack.push_back(predecessor);
            }
        }
        if (PollCancellation(*context.cancellation, &state->operations)) {
            return false;
        }
    }
    return true;
}

std::vector<std::vector<BasicBlockId>> FindStronglyConnectedComponents(
    const std::vector<BasicBlockId> &forwardPostorder, const CfgAnalysisContext &context, bool *cancelled)
{
    std::vector<std::vector<BasicBlockId>> components;
    SccCollectionState state;
    state.assigned.assign(context.reachable->size(), 0U);
    for (auto order = forwardPostorder.rbegin(); order != forwardPostorder.rend(); ++order) {
        if (state.assigned[*order] != 0U) {
            continue;
        }
        components.emplace_back();
        state.assigned[*order] = 1U;
        state.stack.push_back(*order);
        if (!CollectComponentMembers(context, &state, &components.back())) {
            *cancelled = true;
            return {};
        }
        std::sort(components.back().begin(), components.back().end());
    }
    std::sort(components.begin(), components.end(),
              [](const auto &left, const auto &right) { return left.front() < right.front(); });
    *cancelled = context.cancellation->IsCancellationRequested();
    return components;
}

// Computes the immediate-dominator candidate of one block by folding its processed predecessors.
BasicBlockId ComputeBlockDominator(BasicBlockId block, const Adjacency &predecessors,
                                   const std::vector<BasicBlockId> &idom, const std::vector<uint32_t> &rpoPosition)
{
    BasicBlockId newIdom = INVALID_BASIC_BLOCK_ID;
    for (size_t edge = predecessors.offsets[block]; edge < predecessors.offsets[block + 1U]; ++edge) {
        const auto predecessor = predecessors.targets[edge];
        if (idom[predecessor] == INVALID_BASIC_BLOCK_ID) {
            continue;
        }
        newIdom = newIdom == INVALID_BASIC_BLOCK_ID ? predecessor
                                                    : IntersectDominators(newIdom, predecessor, idom, rpoPosition);
    }
    return newIdom;
}

// Iteratively computes immediate dominators over the reverse postorder. The returned result
// carries a failure when the pass limit is exceeded or cancellation was requested.
CfgAnalysisResult ComputeImmediateDominators(const std::vector<BasicBlockId> &rpo, const CfgAnalysisContext &context,
                                             const std::vector<uint32_t> &rpoPosition, const CfgAnalysisLimits &limits,
                                             std::vector<BasicBlockId> *idom)
{
    bool changed = true;
    uint64_t dominatorPass = 0U;
    while (changed) {
        if (dominatorPass++ >= limits.maxDominatorPassCount) {
            return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Dominator analysis exceeds the configured pass limit");
        }
        changed = false;
        for (size_t order = 1U; order < rpo.size(); ++order) {
            const auto block = rpo[order];
            const auto newIdom = ComputeBlockDominator(block, *context.predecessors, *idom, rpoPosition);
            if (newIdom != INVALID_BASIC_BLOCK_ID && (*idom)[block] != newIdom) {
                (*idom)[block] = newIdom;
                changed = true;
            }
        }
        if (context.cancellation->IsCancellationRequested()) {
            return Failure(CfgAnalysisStatus::CANCELLED, "CFG analysis was cancelled");
        }
    }
    return {CfgAnalysisStatus::SUCCESS, nullptr, {}};
}

// Entry classification of one strongly connected component.
struct SccClassification {
    std::vector<BasicBlockId> entries;  // blocks with a reachable predecessor outside the component
    bool cyclic {false};                // the component has multiple blocks or a single-block self-loop
};

// Classifies one strongly connected component: its entry blocks and whether it is cyclic.
SccClassification ClassifyComponent(size_t scc, const std::vector<std::vector<BasicBlockId>> &components,
                                    const CfgAnalysisContext &context, const std::vector<SccId> &blockScc,
                                    BasicBlockId entryBlock)
{
    SccClassification classification;
    classification.cyclic = components[scc].size() > 1U;
    for (const auto block : components[scc]) {
        bool isEntry = block == entryBlock;
        for (size_t edge = context.predecessors->offsets[block]; edge < context.predecessors->offsets[block + 1U];
             ++edge) {
            const auto predecessor = context.predecessors->targets[edge];
            isEntry = isEntry || ((*context.reachable)[predecessor] != 0U && blockScc[predecessor] != scc);
        }
        if (isEntry) {
            classification.entries.push_back(block);
        }
        if (components[scc].size() == 1U) {
            for (size_t edge = context.successors->offsets[block]; edge < context.successors->offsets[block + 1U];
                 ++edge) {
                classification.cyclic = classification.cyclic || context.successors->targets[edge] == block;
            }
        }
    }
    return classification;
}

// Collects the members of natural loops by walking the dominated predecessors of each
// back-edge header. Membership is stamped with a generation counter so the stamps never need
// clearing between loops.
class NaturalLoopCollector {
public:
    NaturalLoopCollector(const CfgAnalysisContext &context, const std::vector<uint32_t> &domPreorder,
                         const std::vector<uint32_t> &domPostorder)
        : context_(context),
          domPreorder_(domPreorder),
          domPostorder_(domPostorder),
          stamps_(context.reachable->size(), 0U)
    {
    }

    // Collects the members of one header's loop. Returns false when cancellation was
    // requested during the walk.
    bool Collect(BasicBlockId header, const std::vector<BasicBlockId> &backEdgeSources,
                 std::vector<BasicBlockId> *members)
    {
        if (++generation_ == 0U) {
            std::fill(stamps_.begin(), stamps_.end(), 0U);
            generation_ = 1U;
        }
        std::vector<BasicBlockId> stack;
        static_cast<void>(Mark(header, members));
        for (const auto source : backEdgeSources) {
            if (Mark(source, members) && source != header) {
                stack.push_back(source);
            }
        }
        while (!stack.empty()) {
            const auto block = stack.back();
            stack.pop_back();
            for (size_t edge = context_.predecessors->offsets[block]; edge < context_.predecessors->offsets[block + 1U];
                 ++edge) {
                const auto predecessor = context_.predecessors->targets[edge];
                if ((*context_.reachable)[predecessor] != 0U &&
                    Dominates(header, predecessor, domPreorder_, domPostorder_) && Mark(predecessor, members) &&
                    predecessor != header) {
                    stack.push_back(predecessor);
                }
            }
            if (context_.cancellation->IsCancellationRequested()) {
                return false;
            }
        }
        return true;
    }

private:
    // Stamps one block with the current generation and appends it to the members. Returns
    // false when the block was already stamped.
    bool Mark(BasicBlockId block, std::vector<BasicBlockId> *members)
    {
        if (stamps_[block] == generation_) {
            return false;
        }
        stamps_[block] = generation_;
        members->push_back(block);
        return true;
    }

    const CfgAnalysisContext &context_;
    const std::vector<uint32_t> &domPreorder_;
    const std::vector<uint32_t> &domPostorder_;
    std::vector<uint32_t> stamps_;
    uint32_t generation_ {0U};
};

// Working state shared by the ordered CfgAnalyzer::Analyze pipeline phases below.
// Each phase fills its own slice and only reads earlier slices, so the pipeline
// keeps the exact ordering of the original single-pass implementation.
struct CfgAnalysisWorkspace {
    Adjacency successors;
    Adjacency predecessors;
    std::vector<uint8_t> reachable;
    std::vector<BasicBlockId> forwardPostorder;
    std::vector<BasicBlockId> rpo;
    std::vector<uint32_t> rpoPosition;
    std::vector<BasicBlockId> idom;
    std::vector<uint32_t> dominatorDepth;
    Adjacency dominatorTree;
    std::vector<uint32_t> domPreorder;
    std::vector<uint32_t> domPostorder;
    CfgAnalysisContext context;
    std::vector<std::vector<BasicBlockId>> components;
    std::vector<SccId> blockScc;
    std::vector<std::vector<BasicBlockId>> componentEntries;
    std::vector<uint8_t> componentCyclic;
    std::vector<uint8_t> componentIrreducible;
    std::vector<std::vector<BasicBlockId>> backEdgeSources;
    std::shared_ptr<BoundedMemoryResource> resource;
    std::shared_ptr<CfgAnalysis> analysis;
};

// Phase 1: adjacency lists, reachability and the two post-orders that fix the
// iteration order of every later phase.
CfgAnalysisResult BuildOrdering(const ControlFlowGraph &graph, const CancellationToken &cancellation,
                                CfgAnalysisWorkspace *work)
{
    const size_t blockCount = graph.blocks.size();
    auto adjacency = BuildAdjacency(graph);
    work->successors = std::move(adjacency.first);
    work->predecessors = std::move(adjacency.second);
    work->reachable.assign(blockCount, 0U);
    work->context.successors = &work->successors;
    work->context.predecessors = &work->predecessors;
    work->context.reachable = &work->reachable;
    work->context.cancellation = &cancellation;
    work->forwardPostorder.reserve(blockCount);
    if (!BuildReachablePostorder(graph.entryBlock, work->successors, cancellation, &work->reachable,
                                 &work->forwardPostorder)) {
        return Failure(CfgAnalysisStatus::CANCELLED, "CFG analysis was cancelled");
    }
    work->rpo.assign(work->forwardPostorder.rbegin(), work->forwardPostorder.rend());
    work->rpoPosition.assign(blockCount, std::numeric_limits<uint32_t>::max());
    for (size_t index = 0U; index < work->rpo.size(); ++index) {
        work->rpoPosition[work->rpo[index]] = static_cast<uint32_t>(index);
    }
    return {CfgAnalysisStatus::SUCCESS, nullptr, {}};
}

// Phase 2: immediate dominators, dominator depths and the numbered dominator tree.
CfgAnalysisResult BuildDominators(const ControlFlowGraph &graph, const CfgAnalysisLimits &limits,
                                  CfgAnalysisWorkspace *work)
{
    const size_t blockCount = graph.blocks.size();
    work->idom.assign(blockCount, INVALID_BASIC_BLOCK_ID);
    work->idom[graph.entryBlock] = graph.entryBlock;
    const auto dominators =
        ComputeImmediateDominators(work->rpo, work->context, work->rpoPosition, limits, &work->idom);
    if (dominators.status != CfgAnalysisStatus::SUCCESS) {
        return dominators;
    }
    work->dominatorDepth.assign(blockCount, 0U);
    for (size_t order = 1U; order < work->rpo.size(); ++order) {
        const auto block = work->rpo[order];
        if (work->idom[block] == INVALID_BASIC_BLOCK_ID) {
            return Failure(CfgAnalysisStatus::INTERNAL_ERROR, "Reachable CFG block has no immediate dominator");
        }
        work->dominatorDepth[block] = work->dominatorDepth[work->idom[block]] + 1U;
    }
    work->dominatorTree = BuildDominatorTree(work->rpo, work->idom);
    work->domPreorder.assign(blockCount, 0U);
    work->domPostorder.assign(blockCount, 0U);
    if (!NumberDominatorTree(graph.entryBlock, work->dominatorTree, *work->context.cancellation, &work->domPreorder,
                             &work->domPostorder)) {
        return Failure(CfgAnalysisStatus::CANCELLED, "CFG analysis was cancelled");
    }
    return {CfgAnalysisStatus::SUCCESS, nullptr, {}};
}

// Phase 3: strongly connected components and their cyclic/irreducible classification.
CfgAnalysisResult BuildComponents(const ControlFlowGraph &graph, CfgAnalysisWorkspace *work)
{
    bool sccCancelled = false;
    work->components = FindStronglyConnectedComponents(work->forwardPostorder, work->context, &sccCancelled);
    if (sccCancelled) {
        return Failure(CfgAnalysisStatus::CANCELLED, "CFG analysis was cancelled");
    }
    if (work->components.size() > std::numeric_limits<SccId>::max()) {
        return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED, "SCC count exceeds the model index range");
    }
    work->blockScc.assign(graph.blocks.size(), INVALID_SCC_ID);
    for (size_t scc = 0U; scc < work->components.size(); ++scc) {
        for (const auto block : work->components[scc]) {
            work->blockScc[block] = static_cast<SccId>(scc);
        }
    }
    work->componentEntries.resize(work->components.size());
    work->componentCyclic.assign(work->components.size(), 0U);
    work->componentIrreducible.assign(work->components.size(), 0U);
    for (size_t scc = 0U; scc < work->components.size(); ++scc) {
        auto classification = ClassifyComponent(scc, work->components, work->context, work->blockScc, graph.entryBlock);
        work->componentEntries[scc] = std::move(classification.entries);
        work->componentCyclic[scc] = classification.cyclic ? 1U : 0U;
        work->componentIrreducible[scc] =
            work->componentCyclic[scc] != 0U && work->componentEntries[scc].size() > 1U ? 1U : 0U;
    }
    return {CfgAnalysisStatus::SUCCESS, nullptr, {}};
}

// Phase 4: per-header back-edge sources, deduplicated in block order.
void BuildBackEdgeSources(const ControlFlowGraph &graph, CfgAnalysisWorkspace *work)
{
    work->backEdgeSources.resize(graph.blocks.size());
    for (const auto &edge : graph.edges) {
        if (!edge.target.has_value() || work->reachable[edge.source] == 0U || work->reachable[*edge.target] == 0U) {
            continue;
        }
        if (Dominates(*edge.target, edge.source, work->domPreorder, work->domPostorder)) {
            work->backEdgeSources[*edge.target].push_back(edge.source);
        }
    }
    for (auto &sources : work->backEdgeSources) {
        std::sort(sources.begin(), sources.end());
        sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    }
}

// Phase 5: allocates the bounded output model and fills the per-block analysis rows.
CfgAnalysisResult BuildBlockInfo(const ControlFlowGraphPtr &graph, const CfgAnalysisLimits &limits,
                                 CfgAnalysisWorkspace *work)
{
    work->resource = std::make_shared<BoundedMemoryResource>(limits.maxModelBytes - sizeof(CfgAnalysis));
    work->analysis = std::make_shared<CfgAnalysis>(work->resource, graph);
    work->analysis->blockInfo.resize(graph->blocks.size());
    for (size_t block = 0U; block < graph->blocks.size(); ++block) {
        auto &info = work->analysis->blockInfo[block];
        info.reachable = work->reachable[block] != 0U;
        if (info.reachable && block != graph->entryBlock) {
            info.immediateDominator = work->idom[block];
            info.dominatorDepth = work->dominatorDepth[block];
        }
        info.stronglyConnectedComponent = work->blockScc[block];
        if (work->blockScc[block] != INVALID_SCC_ID && work->componentIrreducible[work->blockScc[block]] != 0U) {
            info.isIrreducible = true;
        }
    }
    return {CfgAnalysisStatus::SUCCESS, nullptr, {}};
}

// Phase 6: publishes the component classification into the output model.
void FillSccModel(CfgAnalysisWorkspace *work)
{
    work->analysis->stronglyConnectedComponents.reserve(work->components.size());
    for (size_t scc = 0U; scc < work->components.size(); ++scc) {
        work->analysis->stronglyConnectedComponents.emplace_back(work->resource.get());
        auto &output = work->analysis->stronglyConnectedComponents.back();
        output.id = static_cast<SccId>(scc);
        output.cyclic = work->componentCyclic[scc] != 0U;
        output.irreducible = work->componentIrreducible[scc] != 0U;
        output.members.assign(work->components[scc].begin(), work->components[scc].end());
        output.entryBlocks.assign(work->componentEntries[scc].begin(), work->componentEntries[scc].end());
    }
}

// Phase 7: collects natural loops from the back edges and stamps loop headers and depths.
CfgAnalysisResult CollectNaturalLoops(const ControlFlowGraph &graph, const CfgAnalysisLimits &limits,
                                      CfgAnalysisWorkspace *work)
{
    NaturalLoopCollector loopCollector(work->context, work->domPreorder, work->domPostorder);
    uint64_t totalLoopMemberEntries = 0U;
    for (BasicBlockId header = 0U; header < graph.blocks.size(); ++header) {
        if (work->backEdgeSources[header].empty()) {
            continue;
        }
        if (work->analysis->naturalLoops.size() >= limits.maxNaturalLoopCount) {
            return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Natural-loop count exceeds the configured limit");
        }
        std::vector<BasicBlockId> members;
        if (!loopCollector.Collect(header, work->backEdgeSources[header], &members)) {
            return Failure(CfgAnalysisStatus::CANCELLED, "CFG analysis was cancelled");
        }
        std::sort(members.begin(), members.end());
        totalLoopMemberEntries += members.size();
        if (totalLoopMemberEntries > limits.maxLoopMemberEntries) {
            return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED,
                           "Natural-loop membership exceeds the configured limit");
        }
        work->analysis->naturalLoops.emplace_back(work->resource.get());
        auto &loop = work->analysis->naturalLoops.back();
        loop.id = static_cast<uint32_t>(work->analysis->naturalLoops.size() - 1U);
        loop.header = header;
        loop.backEdgeSources.assign(work->backEdgeSources[header].begin(), work->backEdgeSources[header].end());
        loop.members.assign(members.begin(), members.end());
        work->analysis->blockInfo[header].isLoopHeader = true;
        for (const auto member : members) {
            if (work->analysis->blockInfo[member].loopDepth == std::numeric_limits<uint32_t>::max()) {
                return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED, "Natural-loop depth exceeds the model");
            }
            ++work->analysis->blockInfo[member].loopDepth;
        }
    }
    return {CfgAnalysisStatus::SUCCESS, std::move(work->analysis), {}};
}

}  // namespace

CfgAnalysisResult CfgAnalyzer::Analyze(ControlFlowGraphPtr graph, const CfgAnalysisLimits &limits,
                                       const CancellationToken &cancellation) const
{
    if (graph == nullptr) {
        return Failure(CfgAnalysisStatus::INVALID_GRAPH, "Cannot analyze a null CFG");
    }
    const auto validation = ControlFlowGraphValidator::ValidateGraph(*graph, cancellation);
    if (validation.status == CfgStatus::CANCELLED) {
        return Failure(CfgAnalysisStatus::CANCELLED, validation.diagnostic);
    }
    if (validation.status != CfgStatus::SUCCESS) {
        return Failure(CfgAnalysisStatus::INVALID_GRAPH, validation.diagnostic);
    }
    if (limits.maxModelBytes < sizeof(CfgAnalysis)) {
        return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED,
                       "CFG analysis model exceeds the configured byte limit");
    }

    try {
        CfgAnalysisWorkspace work;
        const auto ordering = BuildOrdering(*graph, cancellation, &work);
        if (ordering.status != CfgAnalysisStatus::SUCCESS) {
            return ordering;
        }
        const auto dominators = BuildDominators(*graph, limits, &work);
        if (dominators.status != CfgAnalysisStatus::SUCCESS) {
            return dominators;
        }
        const auto components = BuildComponents(*graph, &work);
        if (components.status != CfgAnalysisStatus::SUCCESS) {
            return components;
        }
        BuildBackEdgeSources(*graph, &work);
        const auto blockInfo = BuildBlockInfo(graph, limits, &work);
        if (blockInfo.status != CfgAnalysisStatus::SUCCESS) {
            return blockInfo;
        }
        FillSccModel(&work);
        return CollectNaturalLoops(*graph, limits, &work);
    } catch (const ModelBudgetExceeded &) {
        return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED,
                       "CFG analysis model exceeds the configured byte limit");
    } catch (const std::bad_alloc &) {
        return Failure(CfgAnalysisStatus::RESOURCE_LIMIT_EXCEEDED, "CFG analysis ran out of memory");
    }
}

}  // namespace panda::bytecode_explorer
