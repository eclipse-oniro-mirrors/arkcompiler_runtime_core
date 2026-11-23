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

#ifndef BYTECODE_EXPLORER_CORE_CFG_CFG_ANALYSIS_H
#define BYTECODE_EXPLORER_CORE_CFG_CFG_ANALYSIS_H

#include "core/cfg/control_flow_graph.h"
#include "core/common/cancellation.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace panda::bytecode_explorer {

using SccId = uint32_t;
constexpr SccId INVALID_SCC_ID = std::numeric_limits<SccId>::max();

struct CfgBlockAnalysis {
    bool reachable {false};
    std::optional<BasicBlockId> immediateDominator;
    uint32_t dominatorDepth {0U};
    SccId stronglyConnectedComponent {INVALID_SCC_ID};
    uint32_t loopDepth {0U};
    bool isLoopHeader {false};
    bool isIrreducible {false};
};

struct StronglyConnectedComponent {
    explicit StronglyConnectedComponent(pmr::memory_resource *resource = NewDeleteResource())
        : members(resource), entryBlocks(resource)
    {
    }

    SccId id {INVALID_SCC_ID};
    bool cyclic {false};
    bool irreducible {false};
    ModelVector<BasicBlockId> members;
    ModelVector<BasicBlockId> entryBlocks;
};

struct NaturalLoop {
    explicit NaturalLoop(pmr::memory_resource *resource = NewDeleteResource())
        : backEdgeSources(resource), members(resource)
    {
    }

    uint32_t id {0U};
    BasicBlockId header {INVALID_BASIC_BLOCK_ID};
    ModelVector<BasicBlockId> backEdgeSources;
    ModelVector<BasicBlockId> members;
};

struct CfgAnalysis {
private:
    // Declared before every PMR field so it is destroyed after them.
    std::shared_ptr<BoundedMemoryResource> ownedResource_;

public:
    CfgAnalysis(std::shared_ptr<BoundedMemoryResource> resource, ControlFlowGraphPtr source)
        : ownedResource_(std::move(resource)),
          graph(std::move(source)),
          blockInfo(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource()),
          stronglyConnectedComponents(ownedResource_ != nullptr ? ownedResource_.get()
                                                                : NewDeleteResource()),
          naturalLoops(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource())
    {
    }

    CfgAnalysis(const CfgAnalysis &) = delete;
    CfgAnalysis &operator=(const CfgAnalysis &) = delete;
    CfgAnalysis(CfgAnalysis &&) = delete;
    CfgAnalysis &operator=(CfgAnalysis &&) = delete;
    ~CfgAnalysis() = default;

    BoundedMemoryResource *OwnedResource() const
    {
        return ownedResource_.get();
    }

    ControlFlowGraphPtr graph;
    ModelVector<CfgBlockAnalysis> blockInfo;
    ModelVector<StronglyConnectedComponent> stronglyConnectedComponents;
    ModelVector<NaturalLoop> naturalLoops;
};

using CfgAnalysisPtr = std::shared_ptr<const CfgAnalysis>;

enum class CfgAnalysisStatus { SUCCESS, INVALID_GRAPH, RESOURCE_LIMIT_EXCEEDED, CANCELLED, INTERNAL_ERROR };

struct CfgAnalysisLimits {
    static constexpr uint64_t DEFAULT_MAX_MODEL_BYTES = 256U * 1024U * 1024U;
    static constexpr uint64_t DEFAULT_MAX_DOMINATOR_PASS_COUNT = 100000U;
    static constexpr uint64_t DEFAULT_MAX_NATURAL_LOOP_COUNT = 500000U;
    static constexpr uint64_t DEFAULT_MAX_LOOP_MEMBER_ENTRIES = 4000000U;

    uint64_t maxModelBytes {DEFAULT_MAX_MODEL_BYTES};
    uint64_t maxDominatorPassCount {DEFAULT_MAX_DOMINATOR_PASS_COUNT};
    uint64_t maxNaturalLoopCount {DEFAULT_MAX_NATURAL_LOOP_COUNT};
    uint64_t maxLoopMemberEntries {DEFAULT_MAX_LOOP_MEMBER_ENTRIES};
};

struct CfgAnalysisResult {
    CfgAnalysisStatus status {CfgAnalysisStatus::INVALID_GRAPH};
    CfgAnalysisPtr analysis;
    std::string diagnostic;
};

class CfgAnalyzer final {
public:
    CfgAnalysisResult Analyze(ControlFlowGraphPtr graph, const CfgAnalysisLimits &limits = {},
                              const CancellationToken &cancellation = {}) const;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_CFG_CFG_ANALYSIS_H
