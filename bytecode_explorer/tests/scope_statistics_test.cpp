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
#include "app/document_loader.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <initializer_list>
#include <memory>
#include <stdexcept>

namespace panda::bytecode_explorer::test {
namespace {

// Fixture entity ids: the file node is the root of both fixture trees.
constexpr EntityId FILE_NODE_ID = 1;
constexpr EntityId ROOT_PARENT_ID = 0;
constexpr EntityId MODULE_A_ID = 2;        // Inventory(): module "a".
constexpr EntityId MODULE_A_EXTRA_ID = 3;  // Inventory(): module "a.extra".
// SemanticInventory() ids: module "m" hosts the class subtree (with a decoy interface)
// and the namespace subtree (with one function).
constexpr EntityId SEMANTIC_MODULE_ID = 2;
constexpr EntityId SEMANTIC_CLASS_ID = 3;
constexpr EntityId SEMANTIC_IFACE_ID = 6;
constexpr EntityId SEMANTIC_NAMESPACE_ID = 7;
constexpr EntityId METHOD_ONE_ID = 4;
constexpr EntityId METHOD_TWO_ID = 5;
constexpr EntityId METHOD_ONE_AGAIN_ID = 9;
constexpr EntityId FUNCTION_THREE_ID = 8;

// Method byte offsets and the code sizes the reader reports for them.
// The 60/600 pair is a decoy that no method references.
constexpr uint64_t METHOD_A_OFFSET = 10;
constexpr uint64_t METHOD_B_OFFSET = 20;
constexpr uint64_t METHOD_C_OFFSET = 30;
constexpr uint64_t METHOD_GUESSED_OFFSET = 40;
constexpr uint64_t METHOD_EXPLICIT_OWNER_OFFSET = 50;
constexpr uint64_t METHOD_DECOY_OFFSET = 60;
constexpr uint64_t CODE_SIZE_A = 100;
constexpr uint64_t CODE_SIZE_B = 200;
constexpr uint64_t CODE_SIZE_C = 300;
constexpr uint64_t CODE_SIZE_GUESSED = 400;
constexpr uint64_t CODE_SIZE_EXPLICIT_OWNER = 500;
constexpr uint64_t CODE_SIZE_DECOY = 600;

// Shared fixture geometry.
constexpr uint64_t FIXTURE_FILE_SIZE = 1000;
constexpr uint64_t METHOD_BACKING_RANGE_SIZE = 28;
constexpr uint64_t CODE_BACKING_RANGE_SIZE = 17;
// Method counts of the physical records.
constexpr size_t FIRST_RECORD_METHOD_COUNT = 3;
constexpr size_t SECOND_RECORD_METHOD_COUNT = 2;
constexpr size_t SEMANTIC_RECORD_METHOD_COUNT = 3;
// Measure() reports one code byte per ten offset bytes.
constexpr uint64_t OFFSET_BYTES_PER_CODE_BYTE = 10;

// Fills a record's method table with the given offsets, in order.
void FillMethodOffsets(PhysicalRecord &record, std::initializer_list<uint64_t> offsets)
{
    size_t index = 0;
    for (const auto offset : offsets) {
        record.methods[index].offset = offset;
        ++index;
    }
}

std::shared_ptr<const StatisticsInventory> Inventory()
{
    StructureIndex structure;
    structure.Add({FILE_NODE_ID, ROOT_PARENT_ID, EntityKind::FILE, "test.abc"});
    EntityNode a(MODULE_A_ID, FILE_NODE_ID, EntityKind::MODULE, "a");
    a.internalName = "a";
    structure.Add(a);
    EntityNode b(MODULE_A_EXTRA_ID, FILE_NODE_ID, EntityKind::MODULE, "a.extra");
    b.internalName = "a.extra";
    structure.Add(b);
    PhysicalProgram physical;
    physical.fileSize = FIXTURE_FILE_SIZE;
    PhysicalRecord first;
    first.name = "a";
    first.isModule = true;
    first.methods.resize(FIRST_RECORD_METHOD_COUNT);
    // The repeated METHOD_A backing must not double count.
    FillMethodOffsets(first, {METHOD_A_OFFSET, METHOD_A_OFFSET, METHOD_B_OFFSET});
    PhysicalRecord second;
    second.name = "a.extra";
    second.isModule = true;
    second.methods.resize(SECOND_RECORD_METHOD_COUNT);
    second.methods[0].offset = METHOD_B_OFFSET;  // Conflicting evidence is unassigned.
    second.methods[1].offset = METHOD_C_OFFSET;
    PhysicalRecord guessed;
    guessed.name = "a.SomeClass";  // Prefix alone is not ownership evidence.
    guessed.methods.resize(1);
    guessed.methods[0].offset = METHOD_GUESSED_OFFSET;
    PhysicalRecord explicitOwner;
    explicitOwner.name = "AnotherRecord";
    explicitOwner.ownerModuleName = "a";
    explicitOwner.methods.resize(1);
    explicitOwner.methods[0].offset = METHOD_EXPLICIT_OWNER_OFFSET;
    physical.records = {first, second, guessed, explicitOwner};
    return StatisticsInventory::Build(physical, structure,
                                      {{METHOD_A_OFFSET, CODE_SIZE_A},
                                       {METHOD_B_OFFSET, CODE_SIZE_B},
                                       {METHOD_C_OFFSET, std::nullopt},
                                       {METHOD_GUESSED_OFFSET, CODE_SIZE_GUESSED},
                                       {METHOD_EXPLICIT_OWNER_OFFSET, CODE_SIZE_EXPLICIT_OWNER},
                                       {METHOD_DECOY_OFFSET, CODE_SIZE_DECOY}});
}

StatisticsMeasurement Measure(uint64_t offset, const CancellationToken &)
{
    return {DisassemblyStatus::SUCCESS, offset, offset / OFFSET_BYTES_PER_CODE_BYTE, {}};
}

}  // namespace

namespace {

std::shared_ptr<const StatisticsInventory> SemanticInventory()
{
    StructureIndex structure;
    structure.Add({FILE_NODE_ID, ROOT_PARENT_ID, EntityKind::FILE, "test.abc"});
    EntityNode module(SEMANTIC_MODULE_ID, FILE_NODE_ID, EntityKind::MODULE, "m");
    module.internalName = "m";
    structure.Add(module);
    EntityNode cls(SEMANTIC_CLASS_ID, SEMANTIC_MODULE_ID, EntityKind::CLASS, "SomeClass");
    cls.origin = EntityOrigin::RECONSTRUCTED;
    structure.Add(cls);
    EntityNode iface(SEMANTIC_IFACE_ID, SEMANTIC_MODULE_ID, EntityKind::INTERFACE, "Empty");
    iface.origin = EntityOrigin::RECONSTRUCTED;
    structure.Add(iface);
    EntityNode space(SEMANTIC_NAMESPACE_ID, SEMANTIC_MODULE_ID, EntityKind::NAMESPACE, "space");
    space.origin = EntityOrigin::RECONSTRUCTED;
    structure.Add(space);
    const auto method = [](EntityId id, EntityId parent, const std::string &name, uint64_t offset,
                           uint64_t codeOffset) {
        EntityNode node(id, parent, EntityKind::METHOD, name);
        node.origin = EntityOrigin::RECONSTRUCTED;
        node.backing = {{PhysicalEntityKind::METHOD, FileRange {offset, METHOD_BACKING_RANGE_SIZE}, "m#" + name},
                        {PhysicalEntityKind::CODE, FileRange {codeOffset, CODE_BACKING_RANGE_SIZE}, "m#" + name}};
        return node;
    };
    structure.Add(method(METHOD_ONE_ID, SEMANTIC_CLASS_ID, "one", METHOD_A_OFFSET, CODE_SIZE_A));
    structure.Add(method(METHOD_TWO_ID, SEMANTIC_CLASS_ID, "two", METHOD_B_OFFSET, CODE_SIZE_B));
    // A duplicate node resolving to the same METHOD offset must not double count.
    structure.Add(method(METHOD_ONE_AGAIN_ID, SEMANTIC_CLASS_ID, "oneAgain", METHOD_A_OFFSET, CODE_SIZE_A));
    const auto function = [](EntityId id, EntityId parent, const std::string &name, uint64_t offset,
                             uint64_t codeOffset) {
        EntityNode node(id, parent, EntityKind::FUNCTION, name);
        node.origin = EntityOrigin::RECONSTRUCTED;
        node.backing = {{PhysicalEntityKind::METHOD, FileRange {offset, METHOD_BACKING_RANGE_SIZE}, "m#" + name},
                        {PhysicalEntityKind::CODE, FileRange {codeOffset, CODE_BACKING_RANGE_SIZE}, "m#" + name}};
        return node;
    };
    structure.Add(function(FUNCTION_THREE_ID, SEMANTIC_NAMESPACE_ID, "three", METHOD_C_OFFSET, CODE_SIZE_C));
    PhysicalProgram physical;
    physical.fileSize = FIXTURE_FILE_SIZE;
    PhysicalRecord record;
    record.name = "m";
    record.isModule = true;
    record.methods.resize(SEMANTIC_RECORD_METHOD_COUNT);
    FillMethodOffsets(record, {METHOD_A_OFFSET, METHOD_B_OFFSET, METHOD_C_OFFSET});
    physical.records = {record};
    return StatisticsInventory::Build(
        physical, structure,
        {{METHOD_A_OFFSET, CODE_SIZE_A}, {METHOD_B_OFFSET, CODE_SIZE_B}, {METHOD_C_OFFSET, CODE_SIZE_C}});
}

}  // namespace

TEST(ScopeStatisticsTest, SemanticScopesAggregateSubtreeMethodTargetsOnce)
{
    const auto inventory = SemanticInventory();
    const auto *cls = inventory->Find(3);
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->kind, EntityKind::CLASS);
    EXPECT_EQ(cls->methodCount, 2U);
    EXPECT_EQ(cls->executableOffsets, (std::vector<uint64_t> {10, 20}));
    const auto *iface = inventory->Find(6);
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->methodCount, 0U);
    EXPECT_TRUE(iface->executableOffsets.empty());
    const auto *space = inventory->Find(7);
    ASSERT_NE(space, nullptr);
    EXPECT_EQ(space->methodCount, 1U);
    EXPECT_EQ(space->executableOffsets, (std::vector<uint64_t> {30}));
    // Physical scopes keep their own accounting: semantic attribution must not
    // inflate the file or module totals.
    const auto *file = inventory->Find(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->methodCount, 3U);
    const auto *module = inventory->Find(2);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->methodCount, 3U);
    EXPECT_EQ(module->executableOffsets, (std::vector<uint64_t> {10, 20, 30}));
}

TEST(ScopeStatisticsTest, UsesPhysicalOwnershipAndDeduplicatesMethods)
{
    const auto inventory = Inventory();
    const auto *file = inventory->Find(1);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->methodCount, 6U);
    EXPECT_EQ(file->executableOffsets.size(), 5U);
    EXPECT_EQ(file->unassignedMethods, 3U);
    EXPECT_EQ(file->unassignedExecutables, 3U);
    ASSERT_TRUE(file->fileSize.has_value());
    EXPECT_EQ(*file->fileSize, 1000U);
    const auto *a = inventory->Find(2);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->methodCount, 2U);
    EXPECT_EQ(a->executableOffsets, (std::vector<uint64_t> {10, 50}));
    EXPECT_FALSE(a->fileSize.has_value());
    EXPECT_EQ(inventory->Find(3)->methodCount, 1U);
    EXPECT_TRUE(inventory->Find(3)->executableOffsets.empty());
    EXPECT_EQ(inventory->Find(999), nullptr);
}

TEST(ScopeStatisticsTest, CalculatesSelectedScopeOnceAndKeepsExactInventory)
{
    auto calls = std::make_shared<std::atomic<size_t>>(0U);
    ScopeStatistics service(Inventory(), [calls](uint64_t offset, const CancellationToken &token) {
        ++*calls;
        return Measure(offset, token);
    });
    auto initial = service.Snapshot(2);
    ASSERT_TRUE(initial.HasValue());
    EXPECT_EQ(initial.Value().phase, StatisticsPhase::NOT_STARTED);
    EXPECT_EQ(initial.Value().methodCount, 2U);
    EXPECT_TRUE(service.Start(2).HasValue());
    service.Wait();
    auto ready = service.Snapshot(2).Value();
    EXPECT_EQ(ready.phase, StatisticsPhase::COMPLETE);
    EXPECT_EQ(ready.codeBytes, 60U);
    EXPECT_EQ(ready.instructionCount, 6U);
    EXPECT_EQ(ready.decodedMethods, 2U);
    EXPECT_EQ(ready.scannedMethods, 2U);
    EXPECT_TRUE(service.Start(2).HasValue());
    service.Wait();
    EXPECT_EQ(calls->load(), 2U);
    EXPECT_FALSE(service.Start(999).HasValue());
    EXPECT_EQ(service.Snapshot(3).Value().executableMethods, 0U);
}

TEST(ScopeStatisticsTest, PartialResultsDoNotPretendFailedMethodsAreZero)
{
    ScopeStatistics service(Inventory(), [](uint64_t offset, const CancellationToken &token) {
        return offset == 50 ? StatisticsMeasurement {DisassemblyStatus::MALFORMED_BYTECODE, 0, 0, "bad bytecode"}
                            : Measure(offset, token);
    });
    ASSERT_TRUE(service.Start(2).HasValue());
    service.Wait();
    const auto state = service.Snapshot(2).Value();
    EXPECT_EQ(state.phase, StatisticsPhase::PARTIAL);
    EXPECT_EQ(state.failedMethods, 1U);
    EXPECT_EQ(state.decodedMethods, 1U);
    EXPECT_EQ(state.codeBytes, 10U);
    EXPECT_EQ(state.instructionCount, 1U);
    EXPECT_FALSE(state.diagnostic.empty());
}

TEST(ScopeStatisticsTest, EnforcesBudgetAndReportsDecoderLimits)
{
    StatisticsLimits limits;
    limits.maxMethods = 1;
    ScopeStatistics service(Inventory(), Measure, limits);
    ASSERT_TRUE(service.Start(2).HasValue());
    service.Wait();
    EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::PARTIAL);
    EXPECT_EQ(service.Snapshot(2).Value().scannedMethods, 1U);
    EXPECT_EQ(service.Snapshot(2).Value().limitedMethods, 1U);
    ScopeStatistics limited(Inventory(), [](uint64_t, const CancellationToken &) {
        return StatisticsMeasurement {DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED, 0, 0, "limit"};
    });
    ASSERT_TRUE(limited.Start(2).HasValue());
    limited.Wait();
    EXPECT_EQ(limited.Snapshot(2).Value().limitedMethods, 2U);
    EXPECT_EQ(limited.Snapshot(2).Value().phase, StatisticsPhase::PARTIAL);
}

TEST(ScopeStatisticsTest, CancellationKeepsStatusResponsiveAndPreventsLatePublication)
{
    auto entered = std::make_shared<std::promise<void>>();
    std::promise<void> release;
    auto released = release.get_future().share();
    ScopeStatistics service(Inventory(), [entered, released](uint64_t offset, const CancellationToken &) {
        entered->set_value();
        released.wait();
        return Measure(offset, {});  // Even a noncooperative decoder must not publish after cancel.
    });
    struct Release {
        std::promise<void> &promise;
        ~Release()
        {
            promise.set_value();
        }
    };
    {
        Release guard {release};
        ASSERT_TRUE(service.Start(2).HasValue());
        ASSERT_EQ(entered->get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::BUILDING);
        EXPECT_TRUE(service.Start(2).HasValue());
        EXPECT_FALSE(service.Start(1).HasValue());  // No unbounded queue of competing scope work.
        EXPECT_TRUE(service.Cancel(2).HasValue());
        EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::CANCELLED);
    }
    service.Wait();
    EXPECT_EQ(service.Snapshot(2).Value().decodedMethods, 0U);
    EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::CANCELLED);
}

TEST(ScopeStatisticsTest, ExceptionsBecomeFailedStatus)
{
    ScopeStatistics service(Inventory(), [](uint64_t, const CancellationToken &) -> StatisticsMeasurement {
        throw std::runtime_error("decode failed");
    });
    ASSERT_TRUE(service.Start(2).HasValue());
    service.Wait();
    EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::FAILED);
    EXPECT_FALSE(service.Snapshot(2).Value().diagnostic.empty());
}

TEST(ScopeStatisticsTest, EmptyScopeCompletesAtZeroAndByteBudgetKeepsKnownSubtotal)
{
    ScopeStatistics empty(Inventory(), Measure);
    ASSERT_TRUE(empty.Start(3).HasValue());
    empty.Wait();
    EXPECT_EQ(empty.Snapshot(3).Value().phase, StatisticsPhase::COMPLETE);
    EXPECT_EQ(empty.Snapshot(3).Value().instructionCount, 0U);
    StatisticsLimits limits;
    limits.maxCodeBytes = 20;
    ScopeStatistics service(Inventory(), Measure, limits);
    ASSERT_TRUE(service.Start(2).HasValue());
    service.Wait();
    const auto result = service.Snapshot(2).Value();
    EXPECT_EQ(result.phase, StatisticsPhase::PARTIAL);
    EXPECT_EQ(result.codeBytes, 10U);
    EXPECT_EQ(result.decodedMethods, 1U);
    EXPECT_EQ(result.limitedMethods, 1U);
}

TEST(ScopeStatisticsTest, DecoderSeesCancellationAndCancelledScopeCanRestart)
{
    auto calls = std::make_shared<std::atomic<unsigned>>(0U);
    auto entered = std::make_shared<std::promise<void>>();
    ScopeStatistics service(Inventory(), [calls, entered](uint64_t offset, const CancellationToken &token) {
        if (++*calls == 1U) {
            entered->set_value();
            while (!token.IsCancellationRequested()) {
                std::this_thread::yield();
            }
            return StatisticsMeasurement {DisassemblyStatus::CANCELLED, 0, 0, {}};
        }
        return Measure(offset, token);
    });
    ASSERT_TRUE(service.Start(2).HasValue());
    ASSERT_EQ(entered->get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(service.Cancel(2).HasValue());
    service.Wait();
    EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::CANCELLED);
    ASSERT_TRUE(service.Start(2).HasValue());
    service.Wait();
    EXPECT_EQ(service.Snapshot(2).Value().phase, StatisticsPhase::COMPLETE);
    EXPECT_EQ(service.Snapshot(2).Value().codeBytes, 60U);
    EXPECT_EQ(calls->load(), 3U);
}

TEST(ScopeStatisticsTest, RealMergedNonMergedAndScriptInventoriesCoverAllIndexedMethods)
{
    for (const auto *path : {BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC,
                             BYTECODE_EXPLORER_STRUCTURE_SCRIPT_TEST_ABC}) {
        auto loaded = LoadExplorerDocument(path);
        ASSERT_TRUE(loaded.HasValue()) << path;
        ASSERT_NE(loaded.Value().statistics, nullptr);
        const auto &inventory = *loaded.Value().statistics;
        const auto *file = inventory.Find(loaded.Value().structure->RootIds().front());
        ASSERT_NE(file, nullptr);
        auto ownedMethods = std::make_shared<uint64_t>(0U);
        auto ownedExecutables = std::make_shared<uint64_t>(0U);
        auto scopes = std::make_shared<size_t>(0U);
        loaded.Value().structure->ForEachNode(
            [inventory, ownedMethods, ownedExecutables, scopes](const EntityNode &node) {
                if (node.kind != EntityKind::MODULE && node.kind != EntityKind::SCRIPT) {
                    return;
                }
                const auto *scope = inventory.Find(node.id);
                ASSERT_NE(scope, nullptr);
                ++*scopes;
                *ownedMethods += scope->methodCount;
                *ownedExecutables += scope->executableOffsets.size();
            });
        EXPECT_GT(*scopes, 0U);
        EXPECT_EQ(file->methodCount, loaded.Value().methods->Size());
        EXPECT_EQ(*ownedMethods + file->unassignedMethods, file->methodCount);
        EXPECT_EQ(*ownedExecutables + file->unassignedExecutables, file->executableOffsets.size());
    }
}

TEST(ScopeStatisticsTest, DestructionCancelsAndJoinsActiveDecode)
{
    auto entered = std::make_shared<std::promise<void>>();
    auto exited = std::make_shared<std::atomic<bool>>(false);
    {
        ScopeStatistics service(Inventory(), [entered, exited](uint64_t, const CancellationToken &token) {
            entered->set_value();
            while (!token.IsCancellationRequested()) {
                std::this_thread::yield();
            }
            *exited = true;
            return StatisticsMeasurement {DisassemblyStatus::CANCELLED, 0, 0, {}};
        });
        ASSERT_TRUE(service.Start(2).HasValue());
        ASSERT_EQ(entered->get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    }
    EXPECT_TRUE(exited->load());
}

}  // namespace panda::bytecode_explorer::test
