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

#ifndef BYTECODE_EXPLORER_CORE_STATISTICS_SCOPE_STATISTICS_H
#define BYTECODE_EXPLORER_CORE_STATISTICS_SCOPE_STATISTICS_H

#include "core/common/result.h"
#include "core/common/task_runner.h"
#include "core/function_disassembler.h"
#include "core/index/structure_index.h"
#include "core/model/physical_program.h"

#include <map>
#include <mutex>

namespace panda::bytecode_explorer {

struct StatisticsScope {
    EntityId id {INVALID_ENTITY_ID};
    EntityKind kind {EntityKind::FILE};
    std::optional<uint64_t> fileSize;
    uint64_t methodCount {0};
    uint64_t unassignedMethods {0};
    uint64_t unassignedExecutables {0};
    std::vector<uint64_t> executableOffsets;
};

class StatisticsInventory final {
public:
    static std::shared_ptr<const StatisticsInventory> Build(const PhysicalProgram &physical,
                                                            const StructureIndex &structure,
                                                            const std::vector<MethodMetadata> &methods);
    const StatisticsScope *Find(EntityId id) const;

private:
    std::map<EntityId, StatisticsScope> scopes_;
};

enum class StatisticsPhase { NOT_STARTED, BUILDING, COMPLETE, PARTIAL, CANCELLED, FAILED };
const char *StatisticsPhaseName(StatisticsPhase phase);

struct StatisticsMeasurement {
    DisassemblyStatus status {DisassemblyStatus::MALFORMED_BYTECODE};
    uint64_t codeBytes {0};
    uint64_t instructionCount {0};
    std::string diagnostic;
};

struct StatisticsLimits {
    uint64_t maxMethods {1000000};
    uint64_t maxCodeBytes {512U * 1024U * 1024U};
};

struct StatisticsSnapshot {
    EntityId id {INVALID_ENTITY_ID};
    EntityKind kind {EntityKind::FILE};
    StatisticsPhase phase {StatisticsPhase::NOT_STARTED};
    std::optional<uint64_t> fileSize;
    uint64_t methodCount {0};
    uint64_t executableMethods {0};
    uint64_t unassignedMethods {0};
    uint64_t unassignedExecutables {0};
    uint64_t scannedMethods {0};
    uint64_t decodedMethods {0};
    uint64_t failedMethods {0};
    uint64_t limitedMethods {0};
    uint64_t codeBytes {0};
    uint64_t instructionCount {0};
    EntityId activeScopeId {INVALID_ENTITY_ID};
    std::string diagnostic;
};

// One worker per document, one active scope, compact result snapshots only.
// Explicit cancellation is document-wide for the selected scope, not tied to a page fetch.
class ScopeStatistics final {
public:
    using Decode = std::function<StatisticsMeasurement(uint64_t, const CancellationToken &)>;
    ScopeStatistics(std::shared_ptr<const StatisticsInventory> inventory, Decode decode, StatisticsLimits limits = {});
    ~ScopeStatistics();
    ScopeStatistics(const ScopeStatistics &) = delete;
    ScopeStatistics &operator=(const ScopeStatistics &) = delete;

    Result<StatisticsSnapshot> Snapshot(EntityId id) const;
    Result<StatisticsSnapshot> Start(EntityId id);
    Result<StatisticsSnapshot> Cancel(EntityId id);
    void Wait();

private:
    StatisticsSnapshot Initial(const StatisticsScope &scope) const;
    void Run(EntityId id, CancellationToken cancellation) noexcept;
    void Calculate(EntityId id, const CancellationToken &cancellation);
    void Fail(EntityId id, const char *message) noexcept;

    std::shared_ptr<const StatisticsInventory> inventory_;
    Decode decode_;
    StatisticsLimits limits_;
    mutable std::mutex mutex_;
    std::map<EntityId, StatisticsSnapshot> snapshots_;
    EntityId active_ {INVALID_ENTITY_ID};
    CancellationSource cancellation_;
    TaskRunner runner_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_STATISTICS_SCOPE_STATISTICS_H
