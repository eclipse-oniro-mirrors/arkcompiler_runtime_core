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

#include "core/statistics/scope_statistics.h"

#include "core/executable_target_resolver.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace panda::bytecode_explorer {

namespace {

// Stored diagnostics are capped so a single broken scope cannot flood the snapshot JSON.
constexpr size_t MAX_DIAGNOSTIC_LENGTH = 512U;

bool IsSemanticScopeKind(const EntityNode &node)
{
    return node.kind == EntityKind::CLASS || node.kind == EntityKind::INTERFACE || node.kind == EntityKind::NAMESPACE;
}

// The helpers below are the extracted stages of StatisticsInventory::Build: scope
// collection, method-owner mapping, physical attribution and semantic attribution.
// Each preserves the original stage order and accounting.

void CollectScopes(const PhysicalProgram &physical, const StructureIndex &structure,
                   std::map<EntityId, StatisticsScope> *scopes, std::unordered_map<std::string, EntityId> *moduleIds,
                   std::vector<EntityId> *files)
{
    structure.ForEachNode([&](const EntityNode &node) {
        if (node.kind != EntityKind::FILE && node.kind != EntityKind::MODULE && node.kind != EntityKind::SCRIPT &&
            !IsSemanticScopeKind(node)) {
            return;
        }
        StatisticsScope scope;
        scope.id = node.id;
        scope.kind = node.kind;
        if (node.kind == EntityKind::FILE) {
            scope.fileSize = physical.fileSize;
            files->push_back(node.id);
        } else if ((node.kind == EntityKind::MODULE || node.kind == EntityKind::SCRIPT) &&
                   node.origin == EntityOrigin::PHYSICAL) {
            auto [found, inserted] = moduleIds->emplace(node.internalName, node.id);
            if (!inserted) {
                found->second = INVALID_ENTITY_ID;
            }
        }
        scopes->emplace(node.id, std::move(scope));
    });
}

std::unordered_map<uint64_t, EntityId> AssignMethodOwners(const PhysicalProgram &physical,
                                                          const std::unordered_map<std::string, EntityId> &moduleIds)
{
    std::unordered_map<uint64_t, EntityId> owners;
    for (const auto &record : physical.records) {
        EntityId owner = INVALID_ENTITY_ID;
        const auto name =
            (record.isModule || record.isScript) ? std::make_optional(record.name) : record.ownerModuleName;
        if (name.has_value()) {
            const auto module = moduleIds.find(*name);
            if (module != moduleIds.end()) {
                owner = module->second;
            }
        }
        for (const auto &method : record.methods) {
            auto [found, inserted] = owners.emplace(method.offset, owner);
            if (!inserted && found->second != owner) {
                found->second = INVALID_ENTITY_ID;
            }
        }
    }
    return owners;
}

void AttributePhysicalMethods(const std::vector<MethodMetadata> &methods,
                              const std::unordered_map<uint64_t, EntityId> &owners, const std::vector<EntityId> &files,
                              std::map<EntityId, StatisticsScope> *scopes)
{
    std::unordered_set<uint64_t> seen;
    for (const auto &method : methods) {
        if (!seen.insert(method.methodOffset).second) {
            continue;
        }
        const auto found = owners.find(method.methodOffset);
        const auto owner = found == owners.end() ? INVALID_ENTITY_ID : found->second;
        const auto add = [&](StatisticsScope &scope) {
            ++scope.methodCount;
            if (method.codeOffset.has_value()) {
                scope.executableOffsets.push_back(method.methodOffset);
            }
        };
        for (auto id : files) {
            auto &scope = scopes->at(id);
            add(scope);
            if (owner == INVALID_ENTITY_ID) {
                ++scope.unassignedMethods;
                scope.unassignedExecutables += method.codeOffset.has_value() ? 1U : 0U;
            }
        }
        if (owner != INVALID_ENTITY_ID) {
            add(scopes->at(owner));
        }
    }
}

// Counts one node's executable targets into a semantic scope, deduplicating
// repeated evidence so a method backing several nodes counts once per scope.
void AddSemanticTargetsToScope(const std::vector<ExecutableTarget> &targets, std::unordered_set<uint64_t> &seen,
                               StatisticsScope &scope)
{
    for (const auto &target : targets) {
        if (seen.insert(target.methodOffset).second) {
            ++scope.methodCount;
            if (target.codeRange.has_value()) {
                scope.executableOffsets.push_back(target.methodOffset);
            }
        }
    }
}

// Semantic attribution: every executable node contributes its METHOD targets
// to each enclosing class/interface/namespace scope. Module scopes keep the
// physical ownership above, so a class total is a drill-down subset of its
// module. Repeated evidence for the same offset (aggregate nodes alongside
// per-method nodes) counts once per scope.
void AttributeSemanticTargets(const StructureIndex &structure, std::map<EntityId, StatisticsScope> *scopes)
{
    std::unordered_map<EntityId, std::unordered_set<uint64_t>> semanticSeen;
    structure.ForEachNode([&](const EntityNode &node) {
        const auto targets = ExecutableTargetResolver::Resolve(node);
        if (targets.empty()) {
            return;
        }
        for (const EntityNode *scopeNode = &node; scopeNode != nullptr;
             scopeNode = scopeNode->parentId == INVALID_ENTITY_ID ? nullptr : structure.Find(scopeNode->parentId)) {
            if (!IsSemanticScopeKind(*scopeNode)) {
                continue;
            }
            AddSemanticTargetsToScope(targets, semanticSeen[scopeNode->id], scopes->at(scopeNode->id));
        }
    });
}

// Accumulates one decoded method into the running snapshot. Returns false when a byte
// or counter budget is exhausted, mirroring the original mid-loop break.
bool AccumulateMeasurement(const StatisticsMeasurement &measured, const StatisticsLimits &limits,
                           StatisticsSnapshot *progress)
{
    if (measured.status == DisassemblyStatus::SUCCESS) {
        if (measured.codeBytes > limits.maxCodeBytes - progress->codeBytes ||
            measured.instructionCount > std::numeric_limits<uint64_t>::max() - progress->instructionCount) {
            progress->limitedMethods += 1U + progress->executableMethods - progress->scannedMethods;
            progress->diagnostic = "Statistics byte or counter budget exhausted";
            return false;
        }
        progress->codeBytes += measured.codeBytes;
        progress->instructionCount += measured.instructionCount;
        ++progress->decodedMethods;
        return true;
    }
    if (measured.status == DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED) {
        ++progress->limitedMethods;
    } else {
        ++progress->failedMethods;
    }
    if (progress->diagnostic.empty()) {
        progress->diagnostic = measured.diagnostic.substr(0, MAX_DIAGNOSTIC_LENGTH);
    }
    return true;
}

}  // namespace

std::shared_ptr<const StatisticsInventory> StatisticsInventory::Build(const PhysicalProgram &physical,
                                                                      const StructureIndex &structure,
                                                                      const std::vector<MethodMetadata> &methods)
{
    auto result = std::make_shared<StatisticsInventory>();
    std::map<EntityId, StatisticsScope> scopes;
    std::unordered_map<std::string, EntityId> moduleIds;
    std::vector<EntityId> files;
    CollectScopes(physical, structure, &scopes, &moduleIds, &files);
    const auto owners = AssignMethodOwners(physical, moduleIds);
    AttributePhysicalMethods(methods, owners, files, &scopes);
    AttributeSemanticTargets(structure, &scopes);
    for (auto &[id, scope] : scopes) {
        (void)id;
        std::sort(scope.executableOffsets.begin(), scope.executableOffsets.end());
    }
    result->scopes_ = std::move(scopes);
    return result;
}

const StatisticsScope *StatisticsInventory::Find(EntityId id) const
{
    const auto found = scopes_.find(id);
    return found == scopes_.end() ? nullptr : &found->second;
}

const char *StatisticsPhaseName(StatisticsPhase phase)
{
    switch (phase) {
        case StatisticsPhase::NOT_STARTED:
            return "NotStarted";
        case StatisticsPhase::BUILDING:
            return "Building";
        case StatisticsPhase::COMPLETE:
            return "Complete";
        case StatisticsPhase::PARTIAL:
            return "Partial";
        case StatisticsPhase::CANCELLED:
            return "Cancelled";
        case StatisticsPhase::FAILED:
            return "Failed";
        default:
            return "Failed";
    }
}

ScopeStatistics::ScopeStatistics(std::shared_ptr<const StatisticsInventory> inventory, Decode decode,
                                 StatisticsLimits limits)
    : inventory_(std::move(inventory)), decode_(std::move(decode)), limits_(limits)
{
}

ScopeStatistics::~ScopeStatistics()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.Cancel();
    }
    Wait();
}

StatisticsSnapshot ScopeStatistics::Initial(const StatisticsScope &scope) const
{
    StatisticsSnapshot result;
    result.id = scope.id;
    result.kind = scope.kind;
    result.fileSize = scope.fileSize;
    result.methodCount = scope.methodCount;
    result.executableMethods = scope.executableOffsets.size();
    result.unassignedMethods = scope.unassignedMethods;
    result.unassignedExecutables = scope.unassignedExecutables;
    return result;
}

Result<StatisticsSnapshot> ScopeStatistics::Snapshot(EntityId id) const
{
    const auto *scope = inventory_ == nullptr ? nullptr : inventory_->Find(id);
    if (scope == nullptr) {
        return Result<StatisticsSnapshot>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Statistics require a File, Module or Script entity"));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = snapshots_.find(id);
    auto snapshot = found == snapshots_.end() ? Initial(*scope) : found->second;
    snapshot.activeScopeId = active_;
    return Result<StatisticsSnapshot>::Success(std::move(snapshot));
}

Result<StatisticsSnapshot> ScopeStatistics::Start(EntityId id)
{
    auto snapshot = Snapshot(id);
    if (!snapshot.HasValue()) {
        return snapshot;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = snapshots_.find(id);
        if (found != snapshots_.end() &&
            (found->second.phase == StatisticsPhase::COMPLETE || found->second.phase == StatisticsPhase::PARTIAL ||
             found->second.phase == StatisticsPhase::BUILDING)) {
            auto result = found->second;
            result.activeScopeId = active_;
            return Result<StatisticsSnapshot>::Success(std::move(result));
        }
        if (active_ != INVALID_ENTITY_ID) {
            return Result<StatisticsSnapshot>::Failure(Status(
                ErrorCode::INVALID_ARGUMENT, "Another statistics calculation is still running; cancel it or wait"));
        }
        auto initial = Initial(*inventory_->Find(id));
        initial.phase = StatisticsPhase::BUILDING;
        cancellation_ = CancellationSource();
        snapshots_.insert_or_assign(id, std::move(initial));
        active_ = id;
        try {
            runner_.Post([this, id, token = cancellation_.Token()] { Run(id, token); });
        } catch (...) {
            snapshots_.at(id).phase = StatisticsPhase::FAILED;
            active_ = INVALID_ENTITY_ID;
        }
    }
    return Snapshot(id);
}

Result<StatisticsSnapshot> ScopeStatistics::Cancel(EntityId id)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ == id && id != INVALID_ENTITY_ID) {
            cancellation_.Cancel();
            snapshots_.at(id).phase = StatisticsPhase::CANCELLED;
        }
    }
    return Snapshot(id);
}

void ScopeStatistics::Wait()
{
    runner_.WaitForIdle();
}

void ScopeStatistics::Fail(EntityId id, const char *message) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto &snapshot = snapshots_.at(id);
    if (snapshot.phase != StatisticsPhase::CANCELLED) {
        snapshot.phase = StatisticsPhase::FAILED;
        try {
            snapshot.diagnostic = std::string(message).substr(0, MAX_DIAGNOSTIC_LENGTH);
        } catch (...) {
            // Phase is still publishable if allocating a diagnostic failed.
        }
    }
}

void ScopeStatistics::Run(EntityId id, CancellationToken cancellation) noexcept
{
    try {
        Calculate(id, cancellation);
    } catch (const std::exception &error) {
        Fail(id, error.what());
    } catch (...) {
        Fail(id, "Statistics calculation failed");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = INVALID_ENTITY_ID;
}

void ScopeStatistics::Calculate(EntityId id, const CancellationToken &cancellation)
{
    const auto &scope = *inventory_->Find(id);
    auto progress = Initial(scope);
    progress.phase = StatisticsPhase::BUILDING;
    for (auto offset : scope.executableOffsets) {
        if (cancellation.IsCancellationRequested()) {
            progress.phase = StatisticsPhase::CANCELLED;
            break;
        }
        if (progress.scannedMethods >= limits_.maxMethods) {
            progress.limitedMethods += progress.executableMethods - progress.scannedMethods;
            progress.diagnostic = "Statistics method budget exhausted";
            break;
        }
        const auto measured = decode_(offset, cancellation);
        if (cancellation.IsCancellationRequested() || measured.status == DisassemblyStatus::CANCELLED) {
            progress.phase = StatisticsPhase::CANCELLED;
            break;
        }
        ++progress.scannedMethods;
        if (!AccumulateMeasurement(measured, limits_, &progress)) {
            break;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshots_.at(id).phase == StatisticsPhase::CANCELLED) {
            return;
        }
        snapshots_.at(id) = progress;
    }
    if (progress.phase == StatisticsPhase::BUILDING) {
        progress.phase = progress.decodedMethods == progress.executableMethods ? StatisticsPhase::COMPLETE
                                                                               : StatisticsPhase::PARTIAL;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshots_.at(id).phase != StatisticsPhase::CANCELLED) {
        snapshots_.at(id) = std::move(progress);
    }
}

}  // namespace panda::bytecode_explorer
