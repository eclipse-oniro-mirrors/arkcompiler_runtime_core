/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

// 20260807927972 Task 02-06: branch_eliminator, serial versus four workers on distinct functions.
// Each worker owns its graph; committed graphs are destroyed before file persistence.

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "scenarios/branch_eliminator/dynamic/branch_eliminator.h"

#include <gtest/gtest.h>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

constexpr auto ENTRY = "branch_eliminator";
const std::string DIR = std::string(ABCKIT_ABC_DIR) + "scenarios/branch_eliminator/dynamic/";
const std::string INPUT = DIR + "branch_eliminator.abc";
const std::string SERIAL_OUT = DIR + "branch_eliminator_serial.abc";
const std::string PARALLEL_OUT = DIR + "branch_eliminator_parallel.abc";
// Final constant value true differs from the origin (false), so a successful transform changes
// the observable output.
constexpr bool CONFIG_FINAL = true;
constexpr int K_WORKER_COUNT = 4;  // parallel worker pool size

// Mirrors scenarios/add_try_catch.../branch_eliminator_test.cpp GetExpectOutput.
static std::string GetExpectedOutput(bool value)
{
    std::ostringstream oss;
    oss << std::boolalpha;
    oss << "Config.isDebug = " << value << "\n";
    oss << "myfoo: Config.isDebug is " << value << "\n";
    oss << "Mybar.test1: Config.isDebug is " << value << "\n";
    oss << "Mybar.test2: Config.isDebug is " << value << "\n";
    return oss.str();
}

// Per-worker body: claim and run the next function task from the shared pool. Extracted to keep the
// TEST_F nesting depth within the checker limit.
static void RunNextBranchTask(BranchEliminator &b, std::vector<std::pair<AbckitCoreFunction *, SuspectsType>> &tasks,
                              std::atomic<size_t> &next)
{
    size_t idx = next.fetch_add(1, std::memory_order_relaxed);
    while (idx < tasks.size()) {
        // D1 (createGraphFromFunction) + D4 (functionSetGraph) on a distinct function.
        b.RunFunctionTask(tasks[idx].first, tasks[idx].second);
        idx = next.fetch_add(1, std::memory_order_relaxed);
    }
}
}  // namespace

class LibAbcKitDynamicScenarioParallelTest : public ::testing::Test {};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicScenarioParallelTest, LibAbcKitTestDynamicBranchEliminatorSerialParallel)
{
    // Origin: Config.isDebug = false (runtime config value).
    auto origin = helpers::ExecuteDynamicAbc(INPUT, ENTRY);
    EXPECT_EQ(origin, GetExpectedOutput(false));

    const std::vector<ConstantInfo> infos = {{"modules/config", "Config", "isDebug", CONFIG_FINAL}};

    // ---- SERIAL baseline: Run() over the whole file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT.c_str(), INPUT.size());
        ASSERT_NE(file, nullptr);
        BranchEliminator b(ABCKIT_VERSION_RELEASE_1_0_0, file, infos);
        b.Run();
        g_impl->writeAbc(file, SERIAL_OUT.c_str(), SERIAL_OUT.size());
        g_impl->closeFile(file);
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT, ENTRY);
    EXPECT_EQ(serialOut, GetExpectedOutput(CONFIG_FINAL));

    // ---- PARALLEL: 4-worker pool over per-function tasks (same shared file) ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT.c_str(), INPUT.size());
        ASSERT_NE(file, nullptr);
        BranchEliminator b(ABCKIT_VERSION_RELEASE_1_0_0, file, infos);
        std::vector<std::pair<AbckitCoreFunction *, SuspectsType>> tasks;
        b.CollectFunctionTasks(tasks);  // main thread: enumeration + suspects (read-only)
        ASSERT_FALSE(tasks.empty());

        std::atomic<size_t> next {0};
        std::vector<std::thread> threads;
        threads.reserve(K_WORKER_COUNT);
        for (int i = 0; i < K_WORKER_COUNT; ++i) {
            threads.emplace_back([&b, &tasks, &next]() { RunNextBranchTask(b, tasks, next); });
        }
        for (auto &t : threads) {
            t.join();
        }
        g_impl->writeAbc(file, PARALLEL_OUT.c_str(), PARALLEL_OUT.size());
        g_impl->closeFile(file);
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT, ENTRY);
    EXPECT_EQ(parallelOut, GetExpectedOutput(CONFIG_FINAL));

    // Serial == parallel (deterministic output).
    EXPECT_EQ(serialOut, parallelOut);
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
