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

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "scenarios/thread_safety/common/scenario_harness.h"

#include "../../../branch_eliminator/dynamic/branch_eliminator.h"
#include "helpers/visit_helper/visit_helper-inl.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test {
namespace {

const std::string DIR = std::string(ABCKIT_ABC_DIR "scenarios/thread_safety/branch_eliminator_parallel/dynamic/");
const std::string INPUT = DIR + "branch_eliminator_parallel.abc";
const std::string OUTPUT = DIR + "branch_eliminator_parallel_modified.abc";
constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr size_t WORKER_COUNT = 4;
constexpr size_t EXPECTED_FUNCTION_COUNT = 8;
constexpr size_t EXPECTED_CONFIG_LOAD_COUNT = 9;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);
const AbckitGraphApi *g_implG = AbckitGetGraphApiImpl(VERSION);
const AbckitIsaApiDynamic *g_dynG = AbckitGetIsaApiDynamicImpl(VERSION);

using TaskKey = std::pair<std::string, std::string>;

const std::array<TaskKey, EXPECTED_FUNCTION_COUNT> EXPECTED_TASKS = {
    TaskKey {"modules/account", "closeAccount"},    TaskKey {"modules/account", "openAccount"},
    TaskKey {"modules/delivery", "cancelDelivery"}, TaskKey {"modules/delivery", "scheduleDelivery"},
    TaskKey {"modules/order", "cancelOrder"},       TaskKey {"modules/order", "processOrder"},
    TaskKey {"modules/payment", "authorize"},       TaskKey {"modules/payment", "refund"}};

const std::vector<ConstantInfo> CONSTANTS = {{"modules/config", "Config", "isDebug", true}};

struct GraphStats {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t branches = 0;
    size_t configLoads = 0;
};

struct WorkerResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t replacedConfigLoads = 0;
    size_t originalTargetBranches = 0;
    size_t remainingTargetBranches = 0;
    size_t committedFunctions = 0;
};

using ScopedAbckitFile = scenario::ScopedFile;

void SetFirstError(AbckitStatus &status, AbckitStatus error)
{
    if (status == ABCKIT_STATUS_NO_ERROR && error != ABCKIT_STATUS_NO_ERROR) {
        status = error;
    }
}

bool RecordLastError(AbckitStatus &status)
{
    SetFirstError(status, g_impl->getLastError());
    return status == ABCKIT_STATUS_NO_ERROR;
}

bool IsExpectedTask(const BranchEliminatorTask &task)
{
    const TaskKey key {task.moduleName, task.functionName};
    return std::find(EXPECTED_TASKS.begin(), EXPECTED_TASKS.end(), key) != EXPECTED_TASKS.end();
}

std::vector<BranchEliminatorTask> GetExpectedTasks(AbckitFile *file, AbckitStatus &status)
{
    BranchEliminator eliminator(VERSION, file, CONSTANTS);
    auto discovered = eliminator.CollectTasks();
    SetFirstError(status, eliminator.GetStatus());
    RecordLastError(status);

    std::vector<BranchEliminatorTask> tasks;
    std::copy_if(discovered.begin(), discovered.end(), std::back_inserter(tasks), IsExpectedTask);
    std::sort(tasks.begin(), tasks.end(), [](const auto &lhs, const auto &rhs) {
        return std::tie(lhs.moduleName, lhs.functionName) < std::tie(rhs.moduleName, rhs.functionName);
    });
    return tasks;
}

bool VisitGraphBlock(AbckitBasicBlock *basicBlock, void *data)
{
    auto &stats = *static_cast<GraphStats *>(data);
    auto *inst = g_implG->bbGetFirstInst(basicBlock);
    if (!RecordLastError(stats.status)) {
        return false;
    }
    while (inst != nullptr) {
        auto opcode = g_dynG->iGetOpcode(inst);
        if (!RecordLastError(stats.status)) {
            return false;
        }
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_IF) {
            ++stats.branches;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
            auto *property = g_implG->iGetString(inst);
            if (!RecordLastError(stats.status) || property == nullptr) {
                SetFirstError(stats.status, ABCKIT_STATUS_INTERNAL_ERROR);
                return false;
            }
            auto *value = g_implI->abckitStringToString(property);
            if (!RecordLastError(stats.status) || value == nullptr) {
                SetFirstError(stats.status, ABCKIT_STATUS_INTERNAL_ERROR);
                return false;
            }
            if (std::string_view(value) == "isDebug") {
                ++stats.configLoads;
            }
        }
        inst = g_implG->iGetNext(inst);
        if (!RecordLastError(stats.status)) {
            return false;
        }
    }
    return true;
}

GraphStats GetGraphStats(AbckitCoreFunction *function)
{
    GraphStats stats;
    auto *graph = g_implI->createGraphFromFunction(function);
    RecordLastError(stats.status);
    if (graph == nullptr) {
        SetFirstError(stats.status, ABCKIT_STATUS_INTERNAL_ERROR);
        return stats;
    }
    if (stats.status != ABCKIT_STATUS_NO_ERROR) {
        g_impl->destroyGraph(graph);
        RecordLastError(stats.status);
        return stats;
    }

    auto visited = g_implG->gVisitBlocksRpo(graph, &stats, VisitGraphBlock);
    RecordLastError(stats.status);
    if (!visited && stats.status == ABCKIT_STATUS_NO_ERROR) {
        stats.status = ABCKIT_STATUS_INTERNAL_ERROR;
    }
    g_impl->destroyGraph(graph);
    RecordLastError(stats.status);
    return stats;
}

void MergeResult(WorkerResult &worker, const BranchEliminatorResult &result)
{
    SetFirstError(worker.status, result.status);
    worker.processedFunctions += result.processedFunctions;
    worker.replacedConfigLoads += result.replacedConfigLoads;
    worker.originalTargetBranches += result.originalTargetBranches;
    worker.remainingTargetBranches += result.remainingTargetBranches;
    worker.committedFunctions += result.committedFunctions;
}

WorkerResult TransformShard(AbckitFile *file, const std::vector<size_t> &shard,
                            const std::vector<BranchEliminatorTask> &tasks)
{
    WorkerResult worker;
    BranchEliminator eliminator(VERSION, file, CONSTANTS);

    for (auto taskIndex : shard) {
        auto result = eliminator.RunTask(tasks[taskIndex]);
        MergeResult(worker, result);
        if (worker.status != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
    return worker;
}

std::string GetExpectedOutput(bool isDebug)
{
    std::stringstream output;
    output << std::boolalpha;
    output << "order.process=" << isDebug << '\n';
    output << "order.cancel=" << isDebug << '\n';
    output << "payment.authorize=" << isDebug << '\n';
    output << "payment.refund=" << isDebug << '\n';
    output << "account.open=" << isDebug << '\n';
    output << "account.close=" << isDebug << '\n';
    output << "delivery.schedule.first=" << isDebug << '\n';
    output << "delivery.schedule.second=" << isDebug << '\n';
    output << "delivery.cancel=" << isDebug << '\n';
    return output.str();
}

using scenario::ShouldVerifyRuntimeOutput;

class AbckitParallelBranchEliminatorScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        ASSERT_EQ(helpers::ExecuteDynamicAbc(INPUT, "branch_eliminator_parallel"), GetExpectedOutput(false));
    }

    ScopedAbckitFile file(g_impl->openAbc(INPUT.c_str(), INPUT.size()));
    ASSERT_NE(file.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    AbckitStatus discoveryStatus = ABCKIT_STATUS_NO_ERROR;
    auto tasks = GetExpectedTasks(file.Get(), discoveryStatus);
    ASSERT_EQ(discoveryStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(tasks.size(), EXPECTED_FUNCTION_COUNT);
    for (size_t index = 0; index < tasks.size(); ++index) {
        EXPECT_EQ(TaskKey(tasks[index].moduleName, tasks[index].functionName), EXPECTED_TASKS[index]);
    }

    size_t expectedBranches = 0;
    size_t expectedConfigLoads = 0;
    for (const auto &task : tasks) {
        auto stats = GetGraphStats(task.function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << task.moduleName << ':' << task.functionName;
        ASSERT_GT(stats.configLoads, 0U) << task.moduleName << ':' << task.functionName;
        // Each config load feeds a source condition; lowering negation may introduce extra IF instructions.
        ASSERT_GE(stats.branches, stats.configLoads) << task.moduleName << ':' << task.functionName;
        expectedBranches += stats.branches;
        expectedConfigLoads += stats.configLoads;
    }
    ASSERT_EQ(expectedConfigLoads, EXPECTED_CONFIG_LOAD_COUNT);

    auto run = scenario::RunShards<WorkerResult>(
        tasks.size(), workerCount, [&](const auto &shard) { return TransformShard(file.Get(), shard, tasks); });
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    const auto &results = run.workers;

    WorkerResult total;
    for (const auto &result : results) {
        EXPECT_EQ(result.status, ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(result.processedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(result.committedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(result.remainingTargetBranches, 0U);
        SetFirstError(total.status, result.status);
        total.processedFunctions += result.processedFunctions;
        total.replacedConfigLoads += result.replacedConfigLoads;
        total.originalTargetBranches += result.originalTargetBranches;
        total.remainingTargetBranches += result.remainingTargetBranches;
        total.committedFunctions += result.committedFunctions;
    }
    ASSERT_EQ(total.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(total.processedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(total.committedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(total.originalTargetBranches, expectedBranches);
    ASSERT_EQ(total.replacedConfigLoads, expectedConfigLoads);
    ASSERT_EQ(total.remainingTargetBranches, 0U);

    g_impl->writeAbc(file.Get(), outputPath.c_str(), outputPath.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    if (verifyRuntimeOutput) {
        observation.output = helpers::ExecuteDynamicAbc(outputPath, "branch_eliminator_parallel");
        ASSERT_EQ(observation.output, GetExpectedOutput(true));
    }

    ScopedAbckitFile reopenedFile(g_impl->openAbc(outputPath.c_str(), outputPath.size()));
    ASSERT_NE(reopenedFile.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    discoveryStatus = ABCKIT_STATUS_NO_ERROR;
    auto reopenedTasks = GetExpectedTasks(reopenedFile.Get(), discoveryStatus);
    ASSERT_EQ(discoveryStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(reopenedTasks.size(), EXPECTED_FUNCTION_COUNT);
    for (const auto &task : reopenedTasks) {
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(task.function, observation));
        auto stats = GetGraphStats(task.function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << task.moduleName << ':' << task.functionName;
        EXPECT_EQ(stats.branches, 0U) << task.moduleName << ':' << task.functionName;
        EXPECT_EQ(stats.configLoads, 0U) << task.moduleName << ':' << task.functionName;
    }
    ASSERT_EQ(reopenedFile.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitParallelBranchEliminatorScenarioTest, LibAbcKitTestParallelBranchEliminatorDynamic)
{
    scenario::Observation serial;
    scenario::Observation parallel;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT + std::string(".serial.abc"), serial));
    ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT, parallel));
    ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
}

}  // namespace
}  // namespace libabckit::test
