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

#include "add_log_instrumenter.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iterator>
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
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

namespace libabckit::test {
namespace {

constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr auto INPUT = ABCKIT_ABC_DIR "scenarios/thread_safety/add_log_parallel/dynamic/add_log_parallel.abc";
constexpr auto OUTPUT = ABCKIT_ABC_DIR "scenarios/thread_safety/add_log_parallel/dynamic/add_log_parallel_modified.abc";
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
    std::string_view entryLog;
    std::string_view bodyLog;
};

const std::array<ExpectedTask, EXPECTED_FUNCTION_COUNT> EXPECTED_TASKS = {
    ExpectedTask {"modules/account", "closeAccount", "enter:account.closeAccount", "body:account.close"},
    ExpectedTask {"modules/account", "openAccount", "enter:account.openAccount", "body:account.open"},
    ExpectedTask {"modules/delivery", "cancelDelivery", "enter:delivery.cancelDelivery", "body:delivery.cancel"},
    ExpectedTask {"modules/delivery", "scheduleDelivery", "enter:delivery.scheduleDelivery", "body:delivery.schedule"},
    ExpectedTask {"modules/order", "cancelOrder", "enter:order.cancelOrder", "body:order.cancel"},
    ExpectedTask {"modules/order", "createOrder", "enter:order.createOrder", "body:order.create"},
    ExpectedTask {"modules/payment", "authorizePayment", "enter:payment.authorizePayment", "body:payment.authorize"},
    ExpectedTask {"modules/payment", "refundPayment", "enter:payment.refundPayment", "body:payment.refund"},
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
    AbckitString *entryMessage = nullptr;
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
        context.first->tasks.push_back({function, context.second, functionName, nullptr});
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

struct PreparedStrings {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    AddLogStrings common;
};

AbckitString *CreateString(AbckitFile *file, std::string_view value, AbckitStatus &status)
{
    auto *result = g_implM->createString(file, value.data(), value.size());
    if (!RecordLastError(status)) {
        return nullptr;
    }
    if (result == nullptr) {
        SetFirstError(status, ABCKIT_STATUS_INTERNAL_ERROR);
        return nullptr;
    }
    return result;
}

PreparedStrings PrepareStrings(AbckitFile *file, std::vector<Task> &tasks)
{
    PreparedStrings result;
    result.common.print = CreateString(file, "print", result.status);
    result.common.date = CreateString(file, "Date", result.status);
    result.common.getTime = CreateString(file, "getTime", result.status);
    result.common.elapsed = CreateString(file, "Ellapsed time:", result.status);
    for (size_t index = 0; index < tasks.size() && result.status == ABCKIT_STATUS_NO_ERROR; ++index) {
        tasks[index].entryMessage = CreateString(file, EXPECTED_TASKS[index].entryLog, result.status);
    }
    return result;
}

struct WorkerResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t instrumentedReturns = 0;
    size_t committedFunctions = 0;
    std::string firstFailedTask;
};

void MergeResult(WorkerResult &worker, const AddLogTransformResult &result, const Task &task)
{
    SetFirstError(worker.status, result.status);
    worker.processedFunctions += result.processedFunctions;
    worker.instrumentedReturns += result.instrumentedReturns;
    worker.committedFunctions += result.committedFunctions;
    if (result.status != ABCKIT_STATUS_NO_ERROR && worker.firstFailedTask.empty()) {
        worker.firstFailedTask = task.moduleName + ':' + task.functionName;
    }
}

WorkerResult TransformShard(const std::vector<Task> &tasks, const std::vector<size_t> &shard,
                            const AddLogStrings &strings)
{
    WorkerResult worker;
    AddLogInstrumenter instrumenter(VERSION, strings);
    for (auto taskIndex : shard) {
        auto result = instrumenter.Instrument(tasks[taskIndex].function, tasks[taskIndex].entryMessage);
        MergeResult(worker, result, tasks[taskIndex]);
        if (worker.status != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
    return worker;
}

using TransformRun = scenario::RunResult<WorkerResult>;

TransformRun TransformTasks(const std::vector<Task> &tasks, const AddLogStrings &strings, size_t workerCount)
{
    return scenario::RunShards<WorkerResult>(tasks.size(), workerCount,
                                             [&](const auto &shard) { return TransformShard(tasks, shard, strings); });
}

AbckitStatus WriteSuccessfulRun(AbckitFile *file, const std::string &outputPath, const TransformRun &run,
                                size_t &writeCalls)
{
    if (run.status != ABCKIT_STATUS_NO_ERROR) {
        return run.status;
    }
    ++writeCalls;
    g_impl->writeAbc(file, outputPath.c_str(), outputPath.size());
    return g_impl->getLastError();
}

struct GraphStats {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::string_view entryLog;
    size_t entryLoads = 0;
    size_t elapsedLoads = 0;
    size_t durationCalculations = 0;
    size_t timeCalls = 0;
};

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
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING) {
            auto value = GetString(g_implG->iGetString(inst), stats.status);
            if (stats.status != ABCKIT_STATUS_NO_ERROR) {
                return false;
            }
            if (value == stats.entryLog) {
                ++stats.entryLoads;
            } else if (value == "Ellapsed time:") {
                ++stats.elapsedLoads;
            }
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_SUB2) {
            ++stats.durationCalculations;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS0) {
            ++stats.timeCalls;
        }
        inst = g_implG->iGetNext(inst);
        if (!RecordLastError(stats.status)) {
            return false;
        }
    }
    return true;
}

GraphStats GetGraphStats(AbckitCoreFunction *function, std::string_view entryLog)
{
    GraphStats stats;
    stats.entryLog = entryLog;
    auto *graph = g_implI->createGraphFromFunction(function);
    if (!RecordLastError(stats.status)) {
        if (graph != nullptr) {
            g_impl->destroyGraph(graph);
            RecordLastError(stats.status);
        }
        return stats;
    }
    if (graph == nullptr) {
        SetFirstError(stats.status, ABCKIT_STATUS_INTERNAL_ERROR);
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

std::string GetOriginalOutput()
{
    std::ostringstream output;
    for (auto index : RUNTIME_ORDER) {
        output << EXPECTED_TASKS[index].bodyLog << '\n';
    }
    return output.str();
}

std::string GetModifiedOutputPattern()
{
    std::ostringstream output;
    for (auto index : RUNTIME_ORDER) {
        output << EXPECTED_TASKS[index].entryLog << '\n';
        output << EXPECTED_TASKS[index].bodyLog << '\n';
        output << "Ellapsed time:\n\\d+\n";
    }
    return output.str();
}

using scenario::ShouldVerifyRuntimeOutput;

}  // namespace

class AbckitParallelAddLogScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        ASSERT_EQ(helpers::ExecuteDynamicAbc(INPUT, "add_log_parallel"), GetOriginalOutput());
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
        auto stats = GetGraphStats(discovery.tasks[index].function, EXPECTED_TASKS[index].entryLog);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(stats.entryLoads, 0U);
        EXPECT_EQ(stats.elapsedLoads, 0U);
        EXPECT_EQ(stats.durationCalculations, 0U);
        EXPECT_EQ(stats.timeCalls, 0U);
    }

    auto strings = PrepareStrings(file.Get(), discovery.tasks);
    ASSERT_EQ(strings.status, ABCKIT_STATUS_NO_ERROR);
    auto run = TransformTasks(discovery.tasks, strings.common, workerCount);
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    size_t processedFunctions = 0;
    size_t instrumentedReturns = 0;
    size_t committedFunctions = 0;
    for (size_t index = 0; index < workerCount; ++index) {
        ASSERT_EQ(run.shardSizes[index], EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].status, ABCKIT_STATUS_NO_ERROR) << run.workers[index].firstFailedTask;
        EXPECT_EQ(run.workers[index].processedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].instrumentedReturns, EXPECTED_FUNCTION_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].committedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        processedFunctions += run.workers[index].processedFunctions;
        instrumentedReturns += run.workers[index].instrumentedReturns;
        committedFunctions += run.workers[index].committedFunctions;
    }
    ASSERT_EQ(processedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(instrumentedReturns, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(committedFunctions, EXPECTED_FUNCTION_COUNT);

    size_t writeCalls = 0;
    ASSERT_EQ(WriteSuccessfulRun(file.Get(), outputPath, run, writeCalls), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(writeCalls, 1U);
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    if (verifyRuntimeOutput) {
        auto output = helpers::ExecuteDynamicAbc(outputPath.c_str(), "add_log_parallel");
        ASSERT_TRUE(helpers::Match(output, GetModifiedOutputPattern())) << output;
        observation.output = scenario::NormalizeElapsedTime(output);
    }

    ScopedAbckitFile reopened(g_impl->openAbc(outputPath.c_str(), outputPath.size()));
    ASSERT_NE(reopened.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto persisted = DiscoverTasks(reopened.Get());
    ASSERT_EQ(persisted.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(persisted.tasks.size(), EXPECTED_FUNCTION_COUNT);
    for (size_t index = 0; index < persisted.tasks.size(); ++index) {
        auto stats = GetGraphStats(persisted.tasks[index].function, EXPECTED_TASKS[index].entryLog);
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(persisted.tasks[index].function, observation));
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(stats.entryLoads, 1U) << persisted.tasks[index].functionName;
        EXPECT_EQ(stats.elapsedLoads, 1U) << persisted.tasks[index].functionName;
        EXPECT_EQ(stats.durationCalculations, 1U) << persisted.tasks[index].functionName;
        EXPECT_EQ(stats.timeCalls, 2U) << persisted.tasks[index].functionName;
    }
    ASSERT_EQ(reopened.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitParallelAddLogScenarioTest, LibAbcKitTestParallelAddLogDynamic)
{
    scenario::Observation serial;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT + std::string(".serial.abc"), serial));
    constexpr size_t ROUNDS = 10;
    for (size_t round = 0; round < ROUNDS; ++round) {
        SCOPED_TRACE("round=" + std::to_string(round));
        scenario::Observation parallel;
        ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT, parallel));
        ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
        std::ostringstream evidence;
        evidence << "workers=4;write=1;input_closed=1;output_reopened=1;output_closed=1\n";
        for (const auto &graph : parallel.graphs) {
            evidence << graph.size() << ':' << graph;
        }
        evidence << "\noutput=" << parallel.output;
        RecordProperty("round_" + std::to_string(round), evidence.str());
    }
}

static void CheckFailedRewritePreservesOutput(bool existingOutput)
{
    const auto outputPath = OUTPUT + std::string(existingOutput ? ".failed-existing.abc" : ".failed-absent.abc");
    const auto removed = std::remove(outputPath.c_str());
    ASSERT_TRUE(removed == 0 || errno == ENOENT);
    std::string original;
    if (existingOutput) {
        std::ifstream input(INPUT, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        original.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        ASSERT_FALSE(original.empty());
        std::ofstream output(outputPath, std::ios::binary);
        output.write(original.data(), static_cast<std::streamsize>(original.size()));
        output.close();
        ASSERT_TRUE(output.good());
    }

    ScopedAbckitFile file(g_impl->openAbc(INPUT, std::char_traits<char>::length(INPUT)));
    ASSERT_NE(file.Get(), nullptr);
    auto discovery = DiscoverTasks(file.Get());
    ASSERT_EQ(discovery.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(discovery.tasks.size(), EXPECTED_FUNCTION_COUNT);
    auto strings = PrepareStrings(file.Get(), discovery.tasks);
    ASSERT_EQ(strings.status, ABCKIT_STATUS_NO_ERROR);
    // Shard zero commits its first real transformation, then fails its second task.
    // Other shards finish their real transformations before RunShards returns.
    discovery.tasks[WORKER_COUNT].entryMessage = nullptr;
    auto run = TransformTasks(discovery.tasks, strings.common, WORKER_COUNT);
    ASSERT_EQ(run.status, ABCKIT_STATUS_BAD_ARGUMENT);
    ASSERT_EQ(run.workers.size(), WORKER_COUNT);
    EXPECT_EQ(run.workers[0].committedFunctions, 1U);
    for (size_t worker = 1; worker < WORKER_COUNT; ++worker) {
        EXPECT_EQ(run.workers[worker].status, ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(run.workers[worker].committedFunctions, 2U);
    }
    size_t writeCalls = 0;
    EXPECT_EQ(WriteSuccessfulRun(file.Get(), outputPath, run, writeCalls), ABCKIT_STATUS_BAD_ARGUMENT);
    EXPECT_EQ(writeCalls, 0U);
    // RunShards has joined every worker; close checks for retained Graph/prepared resources.
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
    std::ifstream output(outputPath, std::ios::binary);
    if (existingOutput) {
        ASSERT_TRUE(output.is_open());
        std::string actual {std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
        EXPECT_TRUE(actual == original) << "Existing output changed: " << original.size() << " -> " << actual.size();
    } else {
        std::error_code error;
        EXPECT_FALSE(std::filesystem::exists(outputPath, error));
        EXPECT_FALSE(error) << error.message();
    }
}

TEST_F(AbckitParallelAddLogScenarioTest, FailedTaskDoesNotCreateOutput)
{
    ASSERT_NO_FATAL_FAILURE(CheckFailedRewritePreservesOutput(false));
}

TEST_F(AbckitParallelAddLogScenarioTest, FailedTaskPreservesExistingOutput)
{
    ASSERT_NO_FATAL_FAILURE(CheckFailedRewritePreservesOutput(true));
}

}  // namespace libabckit::test
