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

#include "try_catch_instrumenter.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "scenarios/thread_safety/common/scenario_harness.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"

namespace libabckit::test {
namespace {

constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr auto INPUT =
    ABCKIT_ABC_DIR "scenarios/thread_safety/add_try_catch_parallel/dynamic/add_try_catch_parallel.abc";
constexpr auto OUTPUT =
    ABCKIT_ABC_DIR "scenarios/thread_safety/add_try_catch_parallel/dynamic/add_try_catch_parallel_modified.abc";
constexpr size_t WORKER_COUNT = 4;
constexpr size_t EXPECTED_FUNCTION_COUNT = 8;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);
const AbckitModifyApi *g_implM = AbckitGetModifyApiImpl(VERSION);
const AbckitGraphApi *g_implG = AbckitGetGraphApiImpl(VERSION);
const AbckitIsaApiDynamic *g_dynG = AbckitGetIsaApiDynamicImpl(VERSION);

struct ExpectedTask {
    std::string_view module;
    std::string_view function;
    std::string_view label;
    bool throws;
};

const std::array<ExpectedTask, EXPECTED_FUNCTION_COUNT> EXPECTED_TASKS = {
    ExpectedTask {"modules/account", "closeAccount", "account.close", false},
    ExpectedTask {"modules/account", "openAccount", "account.open", true},
    ExpectedTask {"modules/delivery", "cancelDelivery", "delivery.cancel", true},
    ExpectedTask {"modules/delivery", "scheduleDelivery", "delivery.schedule", false},
    ExpectedTask {"modules/order", "cancelOrder", "order.cancel", false},
    ExpectedTask {"modules/order", "submitOrder", "order.submit", true},
    ExpectedTask {"modules/payment", "authorizePayment", "payment.authorize", false},
    ExpectedTask {"modules/payment", "refundPayment", "payment.refund", true},
};

const std::array<size_t, EXPECTED_FUNCTION_COUNT> RUNTIME_ORDER = {5, 4, 1, 0, 6, 7, 3, 2};

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

std::string GetString(AbckitString *value, AbckitStatus &status)
{
    if (!RecordLastError(status)) {
        return {};
    }
    if (value == nullptr) {
        SetFirstError(status, ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    auto *data = g_implI->abckitStringToString(value);
    if (!RecordLastError(status)) {
        return {};
    }
    if (data == nullptr) {
        SetFirstError(status, ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    return data;
}

struct Task {
    AbckitCoreFunction *function = nullptr;
    std::string moduleName;
    std::string functionName;
};

struct DiscoveryContext {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::vector<Task> tasks;
};

bool VisitTopLevelFunction(AbckitCoreFunction *function, void *data)
{
    auto &context = *static_cast<std::pair<DiscoveryContext *, std::string> *>(data);
    auto functionName = GetString(g_implI->functionGetName(function), context.first->status);
    if (context.first->status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto expected = std::find_if(EXPECTED_TASKS.begin(), EXPECTED_TASKS.end(), [&](const auto &task) {
        return task.module == context.second && task.function == functionName;
    });
    if (expected != EXPECTED_TASKS.end()) {
        context.first->tasks.push_back({function, context.second, functionName});
    }
    return true;
}

bool VisitModule(AbckitCoreModule *module, void *data)
{
    auto &discovery = *static_cast<DiscoveryContext *>(data);
    auto moduleName = GetString(g_implI->moduleGetName(module), discovery.status);
    if (discovery.status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto expected = std::find_if(EXPECTED_TASKS.begin(), EXPECTED_TASKS.end(),
                                 [&](const auto &task) { return task.module == moduleName; });
    if (expected == EXPECTED_TASKS.end()) {
        return true;
    }
    std::pair<DiscoveryContext *, std::string> context {&discovery, moduleName};
    auto visited = g_implI->moduleEnumerateTopLevelFunctions(module, &context, VisitTopLevelFunction);
    if (!RecordLastError(discovery.status) || !visited) {
        SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    return true;
}

DiscoveryContext DiscoverTasks(AbckitFile *file)
{
    DiscoveryContext result;
    auto visited = g_implI->fileEnumerateModules(file, &result, VisitModule);
    if (!RecordLastError(result.status) || !visited) {
        SetFirstError(result.status, ABCKIT_STATUS_INTERNAL_ERROR);
    }
    std::sort(result.tasks.begin(), result.tasks.end(), [](const auto &lhs, const auto &rhs) {
        return std::tie(lhs.moduleName, lhs.functionName) < std::tie(rhs.moduleName, rhs.functionName);
    });
    return result;
}

struct WorkerResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t insertedCatchRegions = 0;
    size_t committedFunctions = 0;
    std::string firstFailedTask;
};

void MergeResult(WorkerResult &worker, const TryCatchTransformResult &result, const Task &task)
{
    SetFirstError(worker.status, result.status);
    worker.processedFunctions += result.processedFunctions;
    worker.insertedCatchRegions += result.insertedCatchRegions;
    worker.committedFunctions += result.committedFunctions;
    if (result.status != ABCKIT_STATUS_NO_ERROR && worker.firstFailedTask.empty()) {
        worker.firstFailedTask = task.moduleName + ':' + task.functionName;
    }
}

WorkerResult TransformShard(const std::vector<Task> &tasks, const std::vector<size_t> &shard, AbckitString *printName)
{
    WorkerResult worker;
    TryCatchInstrumenter instrumenter(VERSION, printName);
    for (auto taskIndex : shard) {
        auto result = instrumenter.Instrument(tasks[taskIndex].function);
        MergeResult(worker, result, tasks[taskIndex]);
        if (worker.status != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
    return worker;
}

using TransformRun = scenario::RunResult<WorkerResult>;

TransformRun TransformTasks(const std::vector<Task> &tasks, AbckitString *printName, size_t workerCount)
{
    return scenario::RunShards<WorkerResult>(
        tasks.size(), workerCount, [&](const auto &shard) { return TransformShard(tasks, shard, printName); });
}

struct GraphStats {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t tryBegins = 0;
    size_t tryEnds = 0;
    size_t catchBegins = 0;
    size_t catchBlocks = 0;
    size_t catchPhis = 0;
    size_t falseValues = 0;
};

bool VisitGraphBlock(AbckitBasicBlock *basicBlock, void *data)
{
    auto &stats = *static_cast<GraphStats *>(data);
    if (g_implG->bbIsTryBegin(basicBlock)) {
        ++stats.tryBegins;
    }
    if (!RecordLastError(stats.status)) {
        return false;
    }
    if (g_implG->bbIsTryEnd(basicBlock)) {
        ++stats.tryEnds;
    }
    if (!RecordLastError(stats.status)) {
        return false;
    }
    if (g_implG->bbIsCatchBegin(basicBlock)) {
        ++stats.catchBegins;
    }
    if (!RecordLastError(stats.status)) {
        return false;
    }
    if (g_implG->bbIsCatch(basicBlock)) {
        ++stats.catchBlocks;
    }
    if (!RecordLastError(stats.status)) {
        return false;
    }

    auto *inst = g_implG->bbGetFirstInst(basicBlock);
    if (!RecordLastError(stats.status)) {
        return false;
    }
    while (inst != nullptr) {
        auto opcode = g_dynG->iGetOpcode(inst);
        if (!RecordLastError(stats.status)) {
            return false;
        }
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CATCHPHI) {
            ++stats.catchPhis;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LDFALSE) {
            ++stats.falseValues;
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
    if (!RecordLastError(stats.status)) {
        if (graph != nullptr) {
            g_impl->destroyGraph(graph);
            RecordLastError(stats.status);
        }
        return stats;
    }
    if (graph == nullptr) {
        stats.status = ABCKIT_STATUS_INTERNAL_ERROR;
        return stats;
    }
    auto visited = g_implG->gVisitBlocksRpo(graph, &stats, VisitGraphBlock);
    if (!RecordLastError(stats.status) || !visited) {
        SetFirstError(stats.status, ABCKIT_STATUS_INTERNAL_ERROR);
    }
    g_impl->destroyGraph(graph);
    RecordLastError(stats.status);
    return stats;
}

std::string GetExpectedOutput(bool transformed)
{
    std::ostringstream output;
    for (auto index : RUNTIME_ORDER) {
        const auto &task = EXPECTED_TASKS[index];
        output << "body:" << task.label << '\n';
        if (task.throws) {
            if (transformed) {
                output << "Error: failure:" << task.label << '\n';
                output << "result:" << task.label << "=false\n";
            } else {
                output << "outer:" << task.label << "=Error: failure:" << task.label << '\n';
            }
        } else {
            output << "result:" << task.label << "=ok:" << task.label << '\n';
        }
    }
    return output.str();
}

using scenario::ShouldVerifyRuntimeOutput;

}  // namespace

class AbckitParallelAddTryCatchScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        ASSERT_EQ(helpers::ExecuteDynamicAbc(INPUT, "add_try_catch_parallel"), GetExpectedOutput(false));
    }

    ScopedAbckitFile file(g_impl->openAbc(INPUT, std::char_traits<char>::length(INPUT)));
    ASSERT_NE(file.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto discovery = DiscoverTasks(file.Get());
    ASSERT_EQ(discovery.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(discovery.tasks.size(), EXPECTED_FUNCTION_COUNT);
    for (size_t index = 0; index < discovery.tasks.size(); ++index) {
        EXPECT_EQ(discovery.tasks[index].moduleName, EXPECTED_TASKS[index].module);
        EXPECT_EQ(discovery.tasks[index].functionName, EXPECTED_TASKS[index].function);
        auto stats = GetGraphStats(discovery.tasks[index].function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(stats.tryBegins, 0U);
        EXPECT_EQ(stats.tryEnds, 0U);
        EXPECT_EQ(stats.catchBegins, 0U);
        EXPECT_EQ(stats.catchBlocks, 0U);
        EXPECT_EQ(stats.catchPhis, 0U);
        EXPECT_EQ(stats.falseValues, 0U);
    }

    auto *printName = g_implM->createString(file.Get(), "print", std::char_traits<char>::length("print"));
    ASSERT_NE(printName, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto run = TransformTasks(discovery.tasks, printName, workerCount);
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    size_t processedFunctions = 0;
    size_t insertedCatchRegions = 0;
    size_t committedFunctions = 0;
    for (size_t index = 0; index < workerCount; ++index) {
        ASSERT_EQ(run.shardSizes[index], EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].status, ABCKIT_STATUS_NO_ERROR) << run.workers[index].firstFailedTask;
        EXPECT_EQ(run.workers[index].processedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].insertedCatchRegions, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].committedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        processedFunctions += run.workers[index].processedFunctions;
        insertedCatchRegions += run.workers[index].insertedCatchRegions;
        committedFunctions += run.workers[index].committedFunctions;
    }
    ASSERT_EQ(processedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(insertedCatchRegions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(committedFunctions, EXPECTED_FUNCTION_COUNT);

    g_impl->writeAbc(file.Get(), outputPath.c_str(), outputPath.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
    if (verifyRuntimeOutput) {
        observation.output = helpers::ExecuteDynamicAbc(outputPath.c_str(), "add_try_catch_parallel");
        ASSERT_EQ(observation.output, GetExpectedOutput(true));
    }

    ScopedAbckitFile reopened(g_impl->openAbc(outputPath.c_str(), outputPath.size()));
    ASSERT_NE(reopened.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto persisted = DiscoverTasks(reopened.Get());
    ASSERT_EQ(persisted.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(persisted.tasks.size(), EXPECTED_FUNCTION_COUNT);
    for (const auto &task : persisted.tasks) {
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(task.function, observation));
        auto stats = GetGraphStats(task.function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << task.moduleName << ':' << task.functionName;
        EXPECT_EQ(stats.tryBegins, 1U) << task.functionName;
        EXPECT_EQ(stats.tryEnds, 1U) << task.functionName;
        EXPECT_EQ(stats.catchBegins, 1U) << task.functionName;
        // Cleanup may merge the synthetic catch-begin block with the handler body after rebuilding Dynamic IR.
        // Both layouts represent one catch region and preserve the exception and live-in value CatchPhis.
        EXPECT_GE(stats.catchBlocks, 1U) << task.functionName;
        EXPECT_LE(stats.catchBlocks, 2U) << task.functionName;
        EXPECT_EQ(stats.catchPhis, 2U) << task.functionName;
        EXPECT_EQ(stats.falseValues, 1U) << task.functionName;
    }
    ASSERT_EQ(reopened.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitParallelAddTryCatchScenarioTest, LibAbcKitTestParallelAddTryCatchDynamic)
{
    scenario::Observation serial;
    scenario::Observation parallel;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT + std::string(".serial.abc"), serial));
    ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT, parallel));
    ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
}

}  // namespace libabckit::test
