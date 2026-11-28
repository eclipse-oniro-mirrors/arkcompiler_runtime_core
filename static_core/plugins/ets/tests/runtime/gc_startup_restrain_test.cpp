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

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "libarkbase/os/thread.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "runtime/include/runtime.h"
#include "runtime/include/panda_vm.h"
#include "runtime/mem/gc/gc.h"
#include "runtime/mem/gc/gc_trigger.h"

namespace ark::test {

using ets::PandaEtsVM;

static constexpr size_t ON_STARTUP_FOOTPRINT_DIVISOR = 2U;
static constexpr size_t JUST_FINISH_FOOTPRINT_DIVISOR = 4U;
static constexpr size_t EXPECTED_STARTUP_GC_COUNT = 1U;
static constexpr unsigned int NO_OP_OBSERVATION_TIME_MS = 100;
static constexpr unsigned int JUST_FINISH_POLL_INTERVAL_MS = 50;
static constexpr unsigned int JUST_FINISH_POLL_ATTEMPTS = 60;

class GCTriggerTest {
public:
    static size_t GetMinTargetFootprint(const mem::GCTriggerHeap *trigger)
    {
        // Atomic with relaxed order reason: simple getter for test
        return trigger->minTargetFootprint_.load(std::memory_order_relaxed);
    }

    static constexpr size_t GetDefaultMinTargetFootprint()
    {
        return mem::GCTriggerHeap::DEFAULT_MIN_TARGET_FOOTPRINT;
    }
};

class StartupGcCounter : public mem::GCListener {
public:
    void GCFinished(const GCTask &task, [[maybe_unused]] size_t heapSizeBeforeGc,
                    [[maybe_unused]] size_t heapSize) override
    {
        if (task.reason == GCTaskCause::STARTUP_COMPLETE_CAUSE) {
            // Atomic with relaxed order reason: simple counter for test
            counter_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] size_t GetCounter() const
    {
        // Atomic with relaxed order reason: simple getter for test
        return counter_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<size_t> counter_ {0};
};

static PandaEtsVM *GetEtsVM()
{
    return static_cast<PandaEtsVM *>(Runtime::GetCurrent()->GetPandaVM());
}

static size_t GetGCLimit(mem::GC *gc)
{
    auto limit = gc->AdjustStartupLimit(0);
    gc->AdjustStartupLimit(limit);  // Need to restore limit
    return limit;
}

static mem::GCTriggerHeap *GetHeapTrigger(PandaEtsVM *vm)
{
    auto *trigger = vm->GetGCTrigger();
    EXPECT_EQ(trigger->GetType(), mem::GCTriggerType::HEAP_TRIGGER);
    return static_cast<mem::GCTriggerHeap *>(trigger);
}

class GCStartupRestrainTest : public testing::Test {
public:
    GCStartupRestrainTest()
    {
        RuntimeOptions options;
        options.SetCompilerEnableJit(false);
        options.SetShouldLoadBootPandaFiles(true);
        options.SetGcType("g1-gc");
        options.SetLoadRuntimes({"ets"});
        options.SetGcTriggerType("heap-trigger");

        auto stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib == nullptr) {
            std::cerr << "PANDA_STD_LIB env variable should be set and point to etsstdlib.abc" << std::endl;
            std::abort();
        }

        options.SetBootPandaFiles({stdlib});
        Runtime::Create(options);
        GetEtsVM()->GetGC()->AddListener(&counter_);
    }
    ~GCStartupRestrainTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(GCStartupRestrainTest);
    NO_MOVE_SEMANTIC(GCStartupRestrainTest);

protected:
    [[nodiscard]] size_t GetStartupGcCount() const
    {
        return counter_.GetCounter();
    }

private:
    StartupGcCounter counter_;
};

static void WaitForJustFinish(mem::GCTriggerHeap *trigger, size_t justFinishFootprint)
{
    for (unsigned int i = 0; i < JUST_FINISH_POLL_ATTEMPTS; i++) {
        if (GCTriggerTest::GetMinTargetFootprint(trigger) == justFinishFootprint) {
            return;
        }
        os::thread::NativeSleep(JUST_FINISH_POLL_INTERVAL_MS);
    }
}

TEST_F(GCStartupRestrainTest, FallbackTimerAdvancesToJustFinished)
{
    auto *runtime = Runtime::GetCurrent();
    auto *vm = GetEtsVM();
    auto *gc = vm->GetGC();

    auto startLimit = GetGCLimit(gc);

    ASSERT_FALSE(vm->IsPostFork());

    runtime->PreZygoteFork();
    runtime->PostZygoteFork();

    auto *trigger = GetHeapTrigger(vm);
    size_t onStartupFootprint = Runtime::GetOptions().GetHeapSizeLimit() / ON_STARTUP_FOOTPRINT_DIVISOR;
    size_t justFinishFootprint = Runtime::GetOptions().GetHeapSizeLimit() / JUST_FINISH_FOOTPRINT_DIVISOR;

    ASSERT_TRUE(vm->IsPostFork());
    ASSERT_GT(GetGCLimit(gc), startLimit);
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), onStartupFootprint);

    WaitForJustFinish(trigger, justFinishFootprint);

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GetGCLimit(gc), startLimit);
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), justFinishFootprint);

    gc->GetWorkersTaskQueue()->WaitBackgroundTasks();

    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), GCTriggerTest::GetDefaultMinTargetFootprint());
    EXPECT_EQ(GetStartupGcCount(), EXPECTED_STARTUP_GC_COUNT);
}

TEST_F(GCStartupRestrainTest, ColdStartEventPriorToFallback)
{
    auto *runtime = Runtime::GetCurrent();
    auto *vm = GetEtsVM();
    auto *gc = vm->GetGC();

    runtime->PreZygoteFork();
    runtime->PostZygoteFork();

    auto *trigger = GetHeapTrigger(vm);
    size_t justFinishFootprint = Runtime::GetOptions().GetHeapSizeLimit() / JUST_FINISH_FOOTPRINT_DIVISOR;

    vm->NotifyColdStartJustFinish();

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), justFinishFootprint);

    gc->GetWorkersTaskQueue()->WaitBackgroundTasks();

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), GCTriggerTest::GetDefaultMinTargetFootprint());
    EXPECT_EQ(GetStartupGcCount(), EXPECTED_STARTUP_GC_COUNT);
}

TEST_F(GCStartupRestrainTest, EarlyConcurrentColdStartCallbackTest)
{
    auto *runtime = Runtime::GetCurrent();
    auto *vm = GetEtsVM();
    auto *gc = vm->GetGC();

    auto startLimit = GetGCLimit(gc);

    runtime->PreZygoteFork();
    runtime->PostZygoteFork();

    auto *trigger = GetHeapTrigger(vm);
    size_t justFinishFootprint = Runtime::GetOptions().GetHeapSizeLimit() / JUST_FINISH_FOOTPRINT_DIVISOR;

    std::thread earlyCallback([vm]() { vm->NotifyColdStartJustFinish(); });
    earlyCallback.join();

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), justFinishFootprint);

    gc->GetWorkersTaskQueue()->WaitBackgroundTasks();

    EXPECT_EQ(GetGCLimit(gc), startLimit);
    EXPECT_EQ(GetStartupGcCount(), EXPECTED_STARTUP_GC_COUNT);
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), GCTriggerTest::GetDefaultMinTargetFootprint());
}

TEST_F(GCStartupRestrainTest, NotifyColdStartJustFinishIsIdempotent)
{
    auto *runtime = Runtime::GetCurrent();
    auto *vm = GetEtsVM();
    auto *gc = vm->GetGC();

    runtime->PreZygoteFork();
    runtime->PostZygoteFork();

    auto *trigger = GetHeapTrigger(vm);

    vm->NotifyColdStartJustFinish();
    vm->NotifyColdStartJustFinish();

    gc->GetWorkersTaskQueue()->WaitBackgroundTasks();

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GetStartupGcCount(), EXPECTED_STARTUP_GC_COUNT);
    EXPECT_EQ(GCTriggerTest::GetMinTargetFootprint(trigger), GCTriggerTest::GetDefaultMinTargetFootprint());
}

TEST_F(GCStartupRestrainTest, NotifyColdStartJustFinishWithoutForkIsNoOp)
{
    auto *vm = GetEtsVM();
    auto *gc = vm->GetGC();

    auto startLimit = GetGCLimit(gc);

    vm->NotifyColdStartJustFinish();

    EXPECT_FALSE(vm->IsPostFork());
    EXPECT_EQ(GetGCLimit(gc), startLimit);

    os::thread::NativeSleep(NO_OP_OBSERVATION_TIME_MS);
    EXPECT_EQ(GetStartupGcCount(), 0U);
}

}  // namespace ark::test
