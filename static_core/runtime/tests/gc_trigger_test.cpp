/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "runtime/include/runtime.h"
#include "runtime/include/panda_vm.h"
#include "runtime/include/coretypes/line_string.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/mem/gc/gc.h"
#include "runtime/mem/gc/gc_trigger.h"
#include "runtime/mem/mem_stats_default.h"
#include "runtime/mem/mem_stats_additional_info.h"
#include "runtime/handle_scope-inl.h"
#include "test_utils.h"

namespace ark::test {
class GCTriggerTest : public testing::Test {
public:
    GCTriggerTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(false);
        options.SetShouldInitializeIntrinsics(false);
        options.SetGcTriggerType("adaptive-heap-trigger");

        Runtime::Create(options);
        thread_ = ark::MTManagedThread::GetCurrent();
        thread_->ManagedCodeBegin();
    }

    ~GCTriggerTest() override
    {
        thread_->ManagedCodeEnd();
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(GCTriggerTest);
    NO_MOVE_SEMANTIC(GCTriggerTest);

    [[nodiscard]] mem::GCAdaptiveTriggerHeap *CreateGCTriggerHeap() const
    {
        return new mem::GCAdaptiveTriggerHeap(nullptr, nullptr, MIN_HEAP_SIZE,
                                              mem::GCTriggerHeap::DEFAULT_PERCENTAGE_THRESHOLD,
                                              DEFAULT_INCREASE_MULTIPLIER, MIN_EXTRA_HEAP_SIZE, MAX_EXTRA_HEAP_SIZE);
    }

    [[nodiscard]] mem::GCTriggerHeap *CreateHeapTriggerWithMemStats(mem::MemStatsType *memStats) const
    {
        return new mem::GCTriggerHeap(memStats, nullptr, MIN_HEAP_SIZE,
                                      mem::GCTriggerHeap::DEFAULT_PERCENTAGE_THRESHOLD, MIN_EXTRA_HEAP_SIZE,
                                      MAX_EXTRA_HEAP_SIZE);
    }

    [[nodiscard]] mem::GCAdaptiveTriggerHeap *CreateAdaptiveTriggerWithMemStats(mem::MemStatsType *memStats) const
    {
        return new mem::GCAdaptiveTriggerHeap(memStats, nullptr, MIN_HEAP_SIZE,
                                              mem::GCTriggerHeap::DEFAULT_PERCENTAGE_THRESHOLD,
                                              DEFAULT_INCREASE_MULTIPLIER, MIN_EXTRA_HEAP_SIZE, MAX_EXTRA_HEAP_SIZE);
    }

    static size_t GetTargetFootprint(const mem::GCAdaptiveTriggerHeap *trigger)
    {
        // Atomic with relaxed order reason: simple getter for test
        return trigger->targetFootprint_.load(std::memory_order_relaxed);
    }

    static size_t GetTargetFootprint(const mem::GCTriggerHeap *trigger)
    {
        // Atomic with relaxed order reason: simple getter for test
        return trigger->targetFootprint_.load(std::memory_order_relaxed);
    }

    static size_t GetMinTargetFootprint(const mem::GCTriggerHeap *trigger)
    {
        // Atomic with relaxed order reason: simple getter for test
        return trigger->minTargetFootprint_.load(std::memory_order_relaxed);
    }

    static constexpr size_t GetDefaultMinTargetFootprint()
    {
        return mem::GCTriggerHeap::DEFAULT_MIN_TARGET_FOOTPRINT;
    }

    static void FinishPostponeGCIfNeeded(mem::GCTriggerHeap *trigger, mem::GC *gc, size_t bytesInHeap)
    {
        trigger->FinishPostponeGCIfNeeded(gc, bytesInHeap);
    }

protected:
    static constexpr size_t MIN_HEAP_SIZE = 8_MB;
    static constexpr size_t MIN_EXTRA_HEAP_SIZE = 1_MB;
    static constexpr size_t MAX_EXTRA_HEAP_SIZE = 8_MB;
    static constexpr uint32_t DEFAULT_INCREASE_MULTIPLIER = 3U;

private:
    MTManagedThread *thread_ = nullptr;
};

TEST_F(GCTriggerTest, ThresholdTest)
{
    static constexpr size_t BEFORE_HEAP_SIZE = 50_MB;
    static constexpr size_t CURRENT_HEAP_SIZE = MIN_HEAP_SIZE;
    static constexpr size_t FIRST_THRESHOLD = 2U * MIN_HEAP_SIZE;
    static constexpr size_t HEAP_SIZE_AFTER_BASE_TRIGGER = (BEFORE_HEAP_SIZE + CURRENT_HEAP_SIZE) / 2U;
    auto *trigger = CreateGCTriggerHeap();
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);

    trigger->ComputeNewTargetFootprint(task, BEFORE_HEAP_SIZE, CURRENT_HEAP_SIZE);

    ASSERT_EQ(GetTargetFootprint(trigger), FIRST_THRESHOLD);

    trigger->ComputeNewTargetFootprint(task, BEFORE_HEAP_SIZE, CURRENT_HEAP_SIZE);
    ASSERT_EQ(GetTargetFootprint(trigger), HEAP_SIZE_AFTER_BASE_TRIGGER);
    trigger->ComputeNewTargetFootprint(task, BEFORE_HEAP_SIZE, CURRENT_HEAP_SIZE);
    ASSERT_EQ(GetTargetFootprint(trigger), HEAP_SIZE_AFTER_BASE_TRIGGER);
    trigger->ComputeNewTargetFootprint(task, BEFORE_HEAP_SIZE, CURRENT_HEAP_SIZE);
    ASSERT_EQ(GetTargetFootprint(trigger), HEAP_SIZE_AFTER_BASE_TRIGGER);
    trigger->ComputeNewTargetFootprint(task, BEFORE_HEAP_SIZE, CURRENT_HEAP_SIZE);

    // Check that we could to avoid locale triggering
    ASSERT_EQ(GetTargetFootprint(trigger), FIRST_THRESHOLD);

    delete trigger;
}

static constexpr size_t STARTUP_TEST_HEAP_FOOTPRINT = 1_MB;
static constexpr size_t STARTUP_TEST_HIGH_FOOTPRINT = 50_MB;
static constexpr size_t STARTUP_TEST_JUST_FINISH_FOOTPRINT = STARTUP_TEST_HIGH_FOOTPRINT / 2U;
static constexpr size_t STARTUP_TEST_POSTPONE_HEAP_FOOTPRINT = 60_MB;
static constexpr size_t STARTUP_TEST_EXPIRY_ITERATION_COUNT = 500;
static constexpr size_t PERCENT_100 = 100U;

TEST_F(GCTriggerTest, MinTargetFootprintFloorTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->ComputeNewTargetFootprint(task, STARTUP_TEST_HEAP_FOOTPRINT, STARTUP_TEST_HEAP_FOOTPRINT);

    EXPECT_EQ(GetMinTargetFootprint(trigger), STARTUP_TEST_HIGH_FOOTPRINT);
    EXPECT_EQ(GetTargetFootprint(trigger), STARTUP_TEST_HIGH_FOOTPRINT);

    delete trigger;
}

TEST_F(GCTriggerTest, MinTargetFootprintDuringStartupTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();
    trigger->ComputeNewTargetFootprint(task, STARTUP_TEST_HEAP_FOOTPRINT, STARTUP_TEST_HEAP_FOOTPRINT);

    EXPECT_EQ(GetMinTargetFootprint(trigger), STARTUP_TEST_HIGH_FOOTPRINT);
    EXPECT_EQ(GetTargetFootprint(trigger), STARTUP_TEST_HIGH_FOOTPRINT);

    delete trigger;
}

TEST_F(GCTriggerTest, FinishStartupRestrainTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();

    trigger->FinishStartupRestrain();

    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());
    size_t expectedDelta =
        std::max((STARTUP_TEST_HEAP_FOOTPRINT / PERCENT_100) * mem::GCTriggerHeap::DEFAULT_PERCENTAGE_THRESHOLD,
                 MIN_EXTRA_HEAP_SIZE);
    size_t expectedTarget = std::max(STARTUP_TEST_HEAP_FOOTPRINT + expectedDelta, GetDefaultMinTargetFootprint());
    EXPECT_EQ(GetTargetFootprint(trigger), expectedTarget);

    trigger->FinishStartupRestrain();
    EXPECT_EQ(GetTargetFootprint(trigger), expectedTarget);

    trigger->ComputeNewTargetFootprint(task, STARTUP_TEST_HEAP_FOOTPRINT, STARTUP_TEST_HEAP_FOOTPRINT);
    EXPECT_EQ(GetTargetFootprint(trigger), expectedTarget);

    delete trigger;
}

TEST_F(GCTriggerTest, ConcurrentMinTargetFootprintTest)
{
    mem::MemStatsType memStats;
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);

    constexpr size_t SMALL_FOOTPRINT = 10_MB;
    constexpr size_t LARGE_FOOTPRINT = 20_MB;
    constexpr size_t ITERATION_COUNT = 1000;
    constexpr size_t THREAD_COUNT = 4;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([trigger]() {
            for (size_t j = 0; j < ITERATION_COUNT; j++) {
                trigger->SetMinTargetFootprint(SMALL_FOOTPRINT);
                trigger->BeginStartupRestrain();
                trigger->RestoreMinTargetFootprint();
                trigger->SetMinTargetFootprint(LARGE_FOOTPRINT);
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    size_t result = GetMinTargetFootprint(trigger);
    EXPECT_TRUE(result == SMALL_FOOTPRINT || result == LARGE_FOOTPRINT || result == GetDefaultMinTargetFootprint());

    delete trigger;
}

TEST_F(GCTriggerTest, JustFinishStartupRestrainOnStartupTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();
    trigger->JustFinishStartupRestrain(STARTUP_TEST_JUST_FINISH_FOOTPRINT);
    EXPECT_EQ(GetMinTargetFootprint(trigger), STARTUP_TEST_JUST_FINISH_FOOTPRINT);
    EXPECT_EQ(GetTargetFootprint(trigger), STARTUP_TEST_JUST_FINISH_FOOTPRINT);

    trigger->FinishStartupRestrain();
    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());

    delete trigger;
}

TEST_F(GCTriggerTest, JustFinishStartupRestrainAfterFinishedTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();
    trigger->FinishStartupRestrain();

    trigger->JustFinishStartupRestrain(STARTUP_TEST_JUST_FINISH_FOOTPRINT);
    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());

    delete trigger;
}

TEST_F(GCTriggerTest, JustFinishStartupRestrainWithoutBeginTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateHeapTriggerWithMemStats(&memStats);

    trigger->JustFinishStartupRestrain(STARTUP_TEST_JUST_FINISH_FOOTPRINT);
    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());

    delete trigger;
}

static void HammerStartupRestrainFinish(mem::GCTriggerHeap *trigger, size_t threadIndex)
{
    for (size_t j = 0; j < STARTUP_TEST_EXPIRY_ITERATION_COUNT; j++) {
        trigger->FinishStartupRestrain();
        if ((threadIndex + j) % 2U == 0U) {
            trigger->JustFinishStartupRestrain(STARTUP_TEST_JUST_FINISH_FOOTPRINT);
        }
    }
}

TEST_F(GCTriggerTest, ConcurrentStartupRestrainFinishTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateAdaptiveTriggerWithMemStats(&memStats);
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();

    constexpr size_t THREAD_COUNT = 3;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([trigger, i]() { HammerStartupRestrainFinish(trigger, i); });
    }
    for (size_t j = 0; j < STARTUP_TEST_EXPIRY_ITERATION_COUNT; j++) {
        trigger->ComputeNewTargetFootprint(task, STARTUP_TEST_HEAP_FOOTPRINT, STARTUP_TEST_HEAP_FOOTPRINT);
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());

    delete trigger;
}

static void HammerPostponeAndFinish(mem::GCTriggerHeap *trigger, mem::GC *gc, size_t threadIndex)
{
    for (size_t j = 0; j < STARTUP_TEST_EXPIRY_ITERATION_COUNT; j++) {
        GCTriggerTest::FinishPostponeGCIfNeeded(trigger, gc, STARTUP_TEST_POSTPONE_HEAP_FOOTPRINT);
        if ((threadIndex + j) % 2U == 0U) {
            trigger->FinishStartupRestrain();
        } else {
            trigger->JustFinishStartupRestrain(STARTUP_TEST_JUST_FINISH_FOOTPRINT);
        }
    }
}

TEST_F(GCTriggerTest, ConcurrentPostponeAndExpiryTest)
{
    mem::MemStatsType memStats;
    memStats.RecordAllocateObject(STARTUP_TEST_HEAP_FOOTPRINT, SpaceType::SPACE_TYPE_OBJECT);
    auto *trigger = CreateAdaptiveTriggerWithMemStats(&memStats);
    GCTask task(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE);
    auto *gc = Runtime::GetCurrent()->GetPandaVM()->GetGC();

    trigger->SetMinTargetFootprint(STARTUP_TEST_HIGH_FOOTPRINT);
    trigger->BeginStartupRestrain();

    constexpr size_t THREAD_COUNT = 3;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        threads.emplace_back([trigger, gc, i]() { HammerPostponeAndFinish(trigger, gc, i); });
    }
    for (size_t j = 0; j < STARTUP_TEST_EXPIRY_ITERATION_COUNT; j++) {
        trigger->ComputeNewTargetFootprint(task, STARTUP_TEST_HEAP_FOOTPRINT, STARTUP_TEST_HEAP_FOOTPRINT);
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(GetMinTargetFootprint(trigger), GetDefaultMinTargetFootprint());
    EXPECT_GE(GetTargetFootprint(trigger), GetDefaultMinTargetFootprint());

    delete trigger;
}

class GCChecker : public mem::GCListener {
public:
    void GCFinished(const GCTask &task, [[maybe_unused]] size_t heapSizeBeforeGc,
                    [[maybe_unused]] size_t heapSize) override
    {
        reason_ = task.reason;
        counter_++;
    }

    GCTaskCause GetCause() const
    {
        return reason_;
    }

    size_t GetCounter() const
    {
        return counter_;
    }

private:
    GCTaskCause reason_ = GCTaskCause::INVALID_CAUSE;
    size_t counter_ {0};
};

static RuntimeOptions GetRuntimeOptions(const std::string &triggerType)
{
    RuntimeOptions options;
    options.SetShouldLoadBootPandaFiles(false);
    options.SetShouldInitializeIntrinsics(false);
    if (triggerType == "debug-never") {
        options.SetGcTriggerType("debug-never");
        options.SetGcUseNthAllocTrigger(true);
    } else if (triggerType == "pause-time-goal-trigger") {
        options.SetGcTriggerType("pause-time-goal-trigger");
        options.SetRunGcInPlace(true);
        constexpr size_t YOUNG_SIZE = 512 * 1024;
        options.SetYoungSpaceSize(YOUNG_SIZE);
        options.SetInitYoungSpaceSize(YOUNG_SIZE);
        options.SetGcWorkersCount(0);
    } else if (triggerType == "heap-trigger-test") {
        options.SetGcTriggerType("heap-trigger-test");
    } else if (triggerType == "heap-trigger") {
        options.SetGcTriggerType("heap-trigger");
    } else if (triggerType == "adaptive-heap-trigger") {
        options.SetGcTriggerType("adaptive-heap-trigger");
    } else if (triggerType == "no-gc-for-start-up") {
        options.SetGcTriggerType("no-gc-for-start-up");
    } else if (triggerType == "trigger-heap-occupancy") {
        options.SetGcTriggerType("trigger-heap-occupancy");
    } else if (triggerType == "debug") {
        options.SetGcTriggerType("debug");
    } else if (triggerType == "pause-time-goal-trigger") {
        options.SetGcTriggerType("pause-time-goal-trigger");
    } else {
        UNREACHABLE();
    }
    return options;
}

TEST(SchedGCOnNthAllocTriggerTest, TestTrigger)
{
    Runtime::Create(GetRuntimeOptions("debug-never"));
    ManagedThread *thread = ark::ManagedThread::GetCurrent();
    thread->ManagedCodeBegin();
    PandaVM *vm = Runtime::GetCurrent()->GetPandaVM();
    auto *trigger = vm->GetGCTrigger();
    ASSERT_EQ(mem::GCTriggerType::ON_NTH_ALLOC, trigger->GetType());
    auto *schedTrigger = reinterpret_cast<mem::SchedGCOnNthAllocTrigger *>(trigger);
    GCChecker checker;
    vm->GetGC()->AddListener(&checker);

    constexpr size_t STRING_SIZE = 12 * 1024;
    schedTrigger->ScheduleGc(GCTaskCause::YOUNG_GC_CAUSE, 2);
    ark::mem::ObjectAllocator::AllocString(STRING_SIZE);
    EXPECT_FALSE(schedTrigger->IsTriggered());
    EXPECT_EQ(GCTaskCause::INVALID_CAUSE, checker.GetCause());
    ark::mem::ObjectAllocator::AllocString(STRING_SIZE);
    EXPECT_EQ(GCTaskCause::YOUNG_GC_CAUSE, checker.GetCause());
    EXPECT_TRUE(schedTrigger->IsTriggered());

    thread->ManagedCodeEnd();
    Runtime::Destroy();
}

TEST(PauseTimeGoalTriggerTest, TestTrigger)
{
    Runtime::Create(GetRuntimeOptions("pause-time-goal-trigger"));
    auto *thread = ark::ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread s(thread);
        HandleScope<ObjectHeader *> scope(thread);

        auto *runtime = Runtime::GetCurrent();
        auto ctx = runtime->GetLanguageContext(panda_file::SourceLang::PANDA_ASSEMBLY);
        auto *vm = runtime->GetPandaVM();
        auto *trigger = vm->GetGCTrigger();
        ASSERT_EQ(mem::GCTriggerType::PAUSE_TIME_GOAL_TRIGGER, trigger->GetType());
        GCChecker checker;
        vm->GetGC()->AddListener(&checker);

        auto *pauseTimeGoalTrigger = static_cast<ark::mem::PauseTimeGoalTrigger *>(trigger);
        constexpr size_t INIT_TARGET_FOOTPRINT = 1258291;
        ASSERT_EQ(INIT_TARGET_FOOTPRINT, pauseTimeGoalTrigger->GetTargetFootprint());

        constexpr size_t ARRAY_LENGTH = 5 * 32 * 1024;  // big enough to provoke several collections
        VMHandle<coretypes::String> dummy(thread, coretypes::String::CreateEmptyString(ctx, vm));
        VMHandle<coretypes::Array> array(
            thread, ark::mem::ObjectAllocator::AllocArray(ARRAY_LENGTH, ClassRoot::ARRAY_STRING, false));

        size_t expectedCounter = 1;
        size_t startIdx = 0;
        for (size_t i = 0; i < ARRAY_LENGTH; i++) {
            array->Set(i, coretypes::String::CreateEmptyString(ctx, vm));

            if (expectedCounter == checker.GetCounter()) {
                // objects become garbage
                for (size_t j = startIdx; j < i; j++) {
                    array->Set(j, dummy.GetPtr());
                    startIdx = i;
                }
                if (expectedCounter < 2) {
                    ASSERT_EQ(GCTaskCause::YOUNG_GC_CAUSE, checker.GetCause());
                } else if (expectedCounter == 2) {
                    ASSERT_EQ(GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE, checker.GetCause());
                } else if (expectedCounter == 3) {
                    ASSERT_EQ(GCTaskCause::YOUNG_GC_CAUSE, checker.GetCause());
                } else if (expectedCounter > 3) {
                    break;
                }

                expectedCounter++;
            }
        }

        ASSERT_GT(expectedCounter, 3);

        // previous mixed collection should update target footprint
        ASSERT_GT(pauseTimeGoalTrigger->GetTargetFootprint(), INIT_TARGET_FOOTPRINT);
    }
    Runtime::Destroy();
}

TEST(CreateGCTriggerTest, PAUSE_TIME_GOAL_TRIGGER)
{
    Runtime::Create(GetRuntimeOptions("pause-time-goal-trigger"));
    auto *thread = ark::ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread s(thread);
        HandleScope<ObjectHeader *> scope(thread);

        auto *runtime = Runtime::GetCurrent();
        auto *vm = runtime->GetPandaVM();
        auto *trigger = vm->GetGCTrigger();
        ASSERT_EQ(mem::GCTriggerType::PAUSE_TIME_GOAL_TRIGGER, trigger->GetType());
    }
    Runtime::Destroy();
}

TEST(CreateGCTriggerTest, TRIGGER_HEAP_OCCUPANCY)
{
    Runtime::Create(GetRuntimeOptions("trigger-heap-occupancy"));
    auto *thread = ark::ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread s(thread);
        HandleScope<ObjectHeader *> scope(thread);

        auto *runtime = Runtime::GetCurrent();
        auto *vm = runtime->GetPandaVM();
        auto *trigger = vm->GetGCTrigger();
        ASSERT_EQ(mem::GCTriggerType::TRIGGER_HEAP_OCCUPANCY, trigger->GetType());
    }
    Runtime::Destroy();
}

TEST(CreateGCTriggerTest, NO_GC_FOR_START_UP)
{
    Runtime::Create(GetRuntimeOptions("no-gc-for-start-up"));
    auto *thread = ark::ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread s(thread);
        HandleScope<ObjectHeader *> scope(thread);

        auto *runtime = Runtime::GetCurrent();
        auto *vm = runtime->GetPandaVM();
        auto *trigger = vm->GetGCTrigger();
        ASSERT_EQ(mem::GCTriggerType::HEAP_TRIGGER, trigger->GetType());
    }
    Runtime::Destroy();
}

TEST(CreateGCTriggerTest, HEAP_TRIGGER_TEST)
{
    Runtime::Create(GetRuntimeOptions("heap-trigger-test"));
    auto *thread = ark::ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread s(thread);
        HandleScope<ObjectHeader *> scope(thread);

        auto *runtime = Runtime::GetCurrent();
        auto *vm = runtime->GetPandaVM();
        auto *trigger = vm->GetGCTrigger();
        ASSERT_EQ(mem::GCTriggerType::HEAP_TRIGGER, trigger->GetType());
    }
    Runtime::Destroy();
}
}  // namespace ark::test
