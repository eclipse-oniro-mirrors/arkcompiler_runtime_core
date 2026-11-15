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

#ifndef BYTECODE_EXPLORER_CORE_CFG_CFG_BUILDER_H
#define BYTECODE_EXPLORER_CORE_CFG_CFG_BUILDER_H

#include "core/cfg/control_flow_graph.h"
#include "core/common/cancellation.h"

#include <cstdint>
#include <string>

namespace panda::bytecode_explorer {

enum class CfgStatus { SUCCESS, INVALID_DISASSEMBLY, RESOURCE_LIMIT_EXCEEDED, CANCELLED, INTERNAL_ERROR };

struct CfgLimits {
    static constexpr uint64_t DEFAULT_MAX_BLOCK_COUNT = 500000U;
    static constexpr uint64_t DEFAULT_MAX_EDGE_COUNT = 2000000U;
    static constexpr uint64_t DEFAULT_MAX_MODEL_BYTES = 256U * 1024U * 1024U;

    uint64_t maxBlockCount {DEFAULT_MAX_BLOCK_COUNT};
    uint64_t maxEdgeCount {DEFAULT_MAX_EDGE_COUNT};
    uint64_t maxModelBytes {DEFAULT_MAX_MODEL_BYTES};
};

struct CfgValidationResult {
    CfgStatus status {CfgStatus::INVALID_DISASSEMBLY};
    std::string diagnostic;
};

struct CfgBuildResult {
    CfgStatus status {CfgStatus::INVALID_DISASSEMBLY};
    ControlFlowGraphPtr graph;
    std::string diagnostic;
};

class ControlFlowGraphValidator final {
public:
    static CfgValidationResult ValidateDisassembly(const FunctionDisassembly &disassembly,
                                                   const CancellationToken &cancellation = {});
    static CfgValidationResult ValidateGraph(const ControlFlowGraph &graph, const CancellationToken &cancellation = {});
};

class ControlFlowGraphBuilder final {
public:
    CfgBuildResult Build(FunctionDisassemblyPtr disassembly, const CfgLimits &limits = {},
                         const CancellationToken &cancellation = {}) const;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_CFG_CFG_BUILDER_H
