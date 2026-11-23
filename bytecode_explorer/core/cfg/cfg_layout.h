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

#ifndef BYTECODE_EXPLORER_CORE_CFG_CFG_LAYOUT_H
#define BYTECODE_EXPLORER_CORE_CFG_CFG_LAYOUT_H

#include "core/cfg/cfg_analysis.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {

struct CfgPoint {
    double x {0.0};
    double y {0.0};
};

struct CfgNodeGeometry {
    BasicBlockId block {INVALID_BASIC_BLOCK_ID};
    double width {0.0};
    double height {0.0};
    std::optional<CfgPoint> pinnedPosition;
};

struct CfgLayoutRequest {
    uint64_t revision {0U};
    std::vector<CfgNodeGeometry> nodes;
};

struct CfgNodeLayout {
    BasicBlockId block {INVALID_BASIC_BLOCK_ID};
    double x {0.0};
    double y {0.0};
    double width {0.0};
    double height {0.0};
    uint32_t rank {0U};
    bool pinned {false};
};

struct CfgEdgeLayout {
    explicit CfgEdgeLayout(pmr::memory_resource *resource = NewDeleteResource()) : points(resource) {}

    uint32_t edge {0U};
    CfgEdgeKind kind {CfgEdgeKind::FALLTHROUGH};
    bool isBackEdge {false};
    bool isExceptionEdge {false};
    std::optional<uint32_t> outerLane;
    ModelVector<CfgPoint> points;
};

struct CfgLayout {
private:
    // Declared before every PMR field so it is destroyed after them.
    std::shared_ptr<BoundedMemoryResource> ownedResource_;

public:
    CfgLayout(std::shared_ptr<BoundedMemoryResource> resource, CfgAnalysisPtr source)
        : ownedResource_(std::move(resource)),
          analysis(std::move(source)),
          nodes(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource()),
          edges(ownedResource_ != nullptr ? ownedResource_.get() : NewDeleteResource())
    {
    }

    CfgLayout(const CfgLayout &) = delete;
    CfgLayout &operator=(const CfgLayout &) = delete;
    CfgLayout(CfgLayout &&) = delete;
    CfgLayout &operator=(CfgLayout &&) = delete;
    ~CfgLayout() = default;

    CfgAnalysisPtr analysis;
    uint64_t revision {0U};
    double canvasWidth {0.0};
    double canvasHeight {0.0};
    ModelVector<CfgNodeLayout> nodes;
    ModelVector<CfgEdgeLayout> edges;
};

using CfgLayoutPtr = std::shared_ptr<const CfgLayout>;

enum class CfgLayoutStatus { SUCCESS, INVALID_ANALYSIS, INVALID_REQUEST, RESOURCE_LIMIT_EXCEEDED, CANCELLED };

struct CfgLayoutLimits {
    static constexpr uint64_t DEFAULT_MAX_MODEL_BYTES = 256U * 1024U * 1024U;
    static constexpr uint64_t DEFAULT_MAX_NODE_COUNT = 500000U;
    static constexpr uint64_t DEFAULT_MAX_EDGE_COUNT = 2000000U;
    static constexpr uint64_t DEFAULT_MAX_ROUTE_POINT_COUNT = 8000000U;
    static constexpr uint64_t DEFAULT_MAX_OUTER_LANE_COUNT = 100000U;
    static constexpr double DEFAULT_MAX_CANVAS_EXTENT = 100000000.0;

    uint64_t maxModelBytes {DEFAULT_MAX_MODEL_BYTES};
    uint64_t maxNodeCount {DEFAULT_MAX_NODE_COUNT};
    uint64_t maxEdgeCount {DEFAULT_MAX_EDGE_COUNT};
    uint64_t maxRoutePointCount {DEFAULT_MAX_ROUTE_POINT_COUNT};
    uint64_t maxOuterLaneCount {DEFAULT_MAX_OUTER_LANE_COUNT};
    double maxCanvasExtent {DEFAULT_MAX_CANVAS_EXTENT};
};

struct CfgLayoutResult {
    CfgLayoutStatus status {CfgLayoutStatus::INVALID_ANALYSIS};
    CfgLayoutPtr layout;
    std::string diagnostic;
};

class CfgLayoutEngine final {
public:
    CfgLayoutResult Layout(CfgAnalysisPtr analysis, const CfgLayoutRequest &request = {},
                           const CfgLayoutLimits &limits = {}, const CancellationToken &cancellation = {}) const;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_CFG_CFG_LAYOUT_H
