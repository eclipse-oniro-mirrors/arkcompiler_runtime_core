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

#include "scenarios/thread_safety/common/scenario_harness.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "helpers/visit_helper/visit_helper-inl.h"
#include "helpers/helpers_runtime.h"
#include "scenarios/parameter_check/dynamic/api_modifier.h"

namespace libabckit::test {

namespace {

constexpr auto INPUT_PATH =
    ABCKIT_ABC_DIR "scenarios/thread_safety/parameter_check_parallel/dynamic/parameter_check_parallel.abc";
constexpr auto OUTPUT_PATH =
    ABCKIT_ABC_DIR "scenarios/thread_safety/parameter_check_parallel/dynamic/parameter_check_parallel_modified.abc";
constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr std::string_view BASE_MODULE_NAME = "modules/base";
constexpr std::string_view HANDLER_CLASS_NAME = "Handler";
constexpr std::string_view TARGET_METHOD_NAME = "handle";
constexpr size_t WORKER_COUNT = 4;
constexpr size_t EXPECTED_FUNCTION_COUNT = 8;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);
const AbckitGraphApi *g_implG = AbckitGetGraphApiImpl(VERSION);
const AbckitIsaApiDynamic *g_dynG = AbckitGetIsaApiDynamicImpl(VERSION);

const std::string ORIGINAL_OUTPUT =
    "order.create.valid=create-1\n"
    "order.create.invalid=undefined\n"
    "order.cancel.valid=cancel-1\n"
    "order.cancel.invalid=undefined\n"
    "account.open.valid=open-1\n"
    "account.open.invalid=undefined\n"
    "account.close.valid=close-1\n"
    "account.close.invalid=undefined\n"
    "payment.authorize.valid=authorize-1\n"
    "payment.authorize.invalid=undefined\n"
    "payment.refund.valid=refund-1\n"
    "payment.refund.invalid=undefined\n"
    "delivery.schedule.valid=schedule-1\n"
    "delivery.schedule.invalid=undefined\n"
    "delivery.cancel.valid=delivery-cancel-1\n"
    "delivery.cancel.invalid=undefined\n";

const std::string MODIFIED_OUTPUT =
    "order.create.valid=create-1\n"
    "order.create.invalid=-1\n"
    "order.cancel.valid=cancel-1\n"
    "order.cancel.invalid=-1\n"
    "account.open.valid=open-1\n"
    "account.open.invalid=-1\n"
    "account.close.valid=close-1\n"
    "account.close.invalid=-1\n"
    "payment.authorize.valid=authorize-1\n"
    "payment.authorize.invalid=-1\n"
    "payment.refund.valid=refund-1\n"
    "payment.refund.invalid=-1\n"
    "delivery.schedule.valid=schedule-1\n"
    "delivery.schedule.invalid=-1\n"
    "delivery.cancel.valid=delivery-cancel-1\n"
    "delivery.cancel.invalid=-1\n";

struct FunctionTask {
    AbckitCoreFunction *function = nullptr;
    std::string moduleName;
    std::string className;
    std::string functionName;
};

struct WorkerResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t committedFunctions = 0;
    size_t createdCheckBlocks = 0;
    size_t createdFailureBlocks = 0;
    std::string firstFailedTask;
};

using TransformRunResult = scenario::RunResult<WorkerResult>;

struct TransformTotals {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t committedFunctions = 0;
    size_t createdCheckBlocks = 0;
    size_t createdFailureBlocks = 0;
    std::string firstFailedTask;
};

struct GraphStats {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t arrayLoads = 0;
    size_t lengthLoads = 0;
    size_t greaterOrEqualChecks = 0;
    size_t branches = 0;
    size_t minusOneReturns = 0;
};

using ScopedAbckitFile = scenario::ScopedFile;

std::string TaskKey(const FunctionTask &task)
{
    return task.moduleName + ":" + task.className + "." + task.functionName;
}

void SetFirstError(AbckitStatus &status, AbckitStatus error)
{
    if (status == ABCKIT_STATUS_NO_ERROR && error != ABCKIT_STATUS_NO_ERROR) {
        status = error;
    }
}

bool RecordLastError(AbckitStatus &status)
{
    auto error = g_impl->getLastError();
    SetFirstError(status, error);
    return error == ABCKIT_STATUS_NO_ERROR;
}

template <class T>
bool RecordPointer(T *value, AbckitStatus &status)
{
    if (!RecordLastError(status)) {
        return false;
    }
    if (value != nullptr) {
        return true;
    }
    SetFirstError(status, ABCKIT_STATUS_INTERNAL_ERROR);
    return false;
}

std::vector<FunctionTask> DiscoverTasks(AbckitFile *file)
{
    VisitHelper visitor(file, g_impl, g_implI, g_implG, g_dynG);
    std::vector<FunctionTask> tasks;
    std::set<AbckitCoreFunction *> seen;
    visitor.EnumerateModules([&](AbckitCoreModule *module) {
        AbckitCoreImportDescriptor *handlerImport = nullptr;
        visitor.EnumerateModuleImports(module, [&](AbckitCoreImportDescriptor *importDescriptor) {
            auto *importedModule = g_implI->importDescriptorGetImportedModule(importDescriptor);
            if (visitor.GetString(g_implI->moduleGetName(importedModule)) == BASE_MODULE_NAME &&
                visitor.GetString(g_implI->importDescriptorGetName(importDescriptor)) == HANDLER_CLASS_NAME) {
                handlerImport = importDescriptor;
            }
        });
        if (handlerImport == nullptr) {
            return;
        }

        visitor.EnumerateModuleFunctions(module, [&](AbckitCoreFunction *function) {
            visitor.EnumerateFunctionInsts(function, [&](AbckitInst *inst) {
                if (g_dynG->iGetOpcode(inst) != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR ||
                    g_dynG->iGetImportDescriptor(inst) != handlerImport) {
                    return;
                }
                visitor.EnumerateInstUsers(inst, [&](AbckitInst *user) {
                    if (g_dynG->iGetOpcode(user) != ABCKIT_ISA_API_DYNAMIC_OPCODE_DEFINECLASSWITHBUFFER) {
                        return;
                    }
                    auto *classInitializer = g_implG->iGetFunction(user);
                    auto *klass = g_implI->functionGetParentClass(classInitializer);
                    if (klass == nullptr) {
                        return;
                    }
                    visitor.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
                        auto methodName = visitor.GetString(g_implI->functionGetName(method));
                        if (methodName != TARGET_METHOD_NAME || !seen.insert(method).second) {
                            return;
                        }
                        tasks.push_back({method, std::string(visitor.GetString(g_implI->moduleGetName(module))),
                                         std::string(visitor.GetString(g_implI->classGetName(klass))),
                                         std::string(methodName)});
                    });
                });
            });
        });
    });

    std::sort(tasks.begin(), tasks.end(), [](const FunctionTask &lhs, const FunctionTask &rhs) {
        return std::tie(lhs.moduleName, lhs.className, lhs.functionName) <
               std::tie(rhs.moduleName, rhs.className, rhs.functionName);
    });
    return tasks;
}

void MergeResult(WorkerResult &worker, const ParameterCheckTransformResult &result, const FunctionTask &task)
{
    ++worker.processedFunctions;
    worker.committedFunctions += result.committedFunctions;
    worker.createdCheckBlocks += result.createdCheckBlocks;
    worker.createdFailureBlocks += result.createdFailureBlocks;
    if (worker.status == ABCKIT_STATUS_NO_ERROR && result.status != ABCKIT_STATUS_NO_ERROR) {
        worker.status = result.status;
        worker.firstFailedTask = TaskKey(task);
    }
}

WorkerResult TransformShard(AbckitFile *file, const std::vector<size_t> &shard, const std::vector<FunctionTask> &tasks)
{
    WorkerResult worker;
    ApiModifier modifier(
        VERSION, file, g_impl,
        {std::string(BASE_MODULE_NAME), std::string(HANDLER_CLASS_NAME), std::string(TARGET_METHOD_NAME)});

    for (auto taskIndex : shard) {
        auto result = modifier.AddParamChecker(tasks[taskIndex].function);
        MergeResult(worker, result, tasks[taskIndex]);
        if (worker.status != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
    return worker;
}

TransformRunResult TransformTasks(AbckitFile *file, const std::vector<FunctionTask> &tasks, size_t workerCount)
{
    return scenario::RunShards<WorkerResult>(tasks.size(), workerCount,
                                             [&](const auto &shard) { return TransformShard(file, shard, tasks); });
}

TransformTotals GetTransformTotals(const TransformRunResult &run)
{
    TransformTotals totals;
    for (const auto &worker : run.workers) {
        SetFirstError(totals.status, worker.status);
        totals.processedFunctions += worker.processedFunctions;
        totals.committedFunctions += worker.committedFunctions;
        totals.createdCheckBlocks += worker.createdCheckBlocks;
        totals.createdFailureBlocks += worker.createdFailureBlocks;
        if (totals.firstFailedTask.empty() && !worker.firstFailedTask.empty()) {
            totals.firstFailedTask = worker.firstFailedTask;
        }
    }
    return totals;
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
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYVALUE) {
            ++stats.arrayLoads;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
            auto *property = g_implG->iGetString(inst);
            if (!RecordPointer(property, stats.status)) {
                return false;
            }
            auto *value = g_implI->abckitStringToString(property);
            if (!RecordPointer(value, stats.status)) {
                return false;
            }
            if (std::string_view(value) == "length") {
                ++stats.lengthLoads;
            }
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_GREATEREQ) {
            ++stats.greaterOrEqualChecks;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_IF) {
            ++stats.branches;
        } else if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURN) {
            auto inputCount = g_implG->iGetInputCount(inst);
            if (!RecordLastError(stats.status)) {
                return false;
            }
            if (inputCount > 0) {
                auto *returnValue = g_implG->iGetInput(inst, 0);
                if (!RecordPointer(returnValue, stats.status)) {
                    return false;
                }
                auto inputOpcode = g_dynG->iGetOpcode(returnValue);
                if (!RecordLastError(stats.status)) {
                    return false;
                }
                if (inputOpcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT) {
                    auto constantValue = g_implG->iGetConstantValueI32(returnValue);
                    if (!RecordLastError(stats.status)) {
                        return false;
                    }
                    if (constantValue == -1) {
                        ++stats.minusOneReturns;
                    }
                }
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
    if (!RecordPointer(graph, stats.status)) {
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

using scenario::ShouldVerifyRuntimeOutput;

}  // namespace

class AbckitParallelParameterCheckScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        ASSERT_EQ(helpers::ExecuteDynamicAbc(INPUT_PATH, "parameter_check_parallel"), ORIGINAL_OUTPUT);
    }

    ScopedAbckitFile file(g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH)));
    ASSERT_NE(file.Get(), nullptr);
    auto tasks = DiscoverTasks(file.Get());
    ASSERT_EQ(tasks.size(), EXPECTED_FUNCTION_COUNT);
    const std::vector<std::string> expectedTasks = {
        "modules/account_handlers:CloseAccountHandler.handle",
        "modules/account_handlers:OpenAccountHandler.handle",
        "modules/delivery_handlers:CancelDeliveryHandler.handle",
        "modules/delivery_handlers:ScheduleDeliveryHandler.handle",
        "modules/order_handlers:CancelOrderHandler.handle",
        "modules/order_handlers:CreateOrderHandler.handle",
        "modules/payment_handlers:AuthorizePaymentHandler.handle",
        "modules/payment_handlers:RefundPaymentHandler.handle",
    };
    for (size_t index = 0; index < tasks.size(); ++index) {
        ASSERT_EQ(TaskKey(tasks[index]), expectedTasks[index]);
        auto stats = GetGraphStats(tasks[index].function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << TaskKey(tasks[index]);
        ASSERT_GT(stats.arrayLoads, 0U) << TaskKey(tasks[index]);
        ASSERT_EQ(stats.lengthLoads, 0U) << TaskKey(tasks[index]);
        ASSERT_EQ(stats.greaterOrEqualChecks, 0U) << TaskKey(tasks[index]);
        ASSERT_EQ(stats.branches, 0U) << TaskKey(tasks[index]);
        ASSERT_EQ(stats.minusOneReturns, 0U) << TaskKey(tasks[index]);
    }

    auto run = TransformTasks(file.Get(), tasks, workerCount);
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(run.workers.size(), workerCount);
    ASSERT_EQ(run.shardSizes.size(), workerCount);

    for (size_t index = 0; index < workerCount; ++index) {
        ASSERT_EQ(run.shardSizes[index], EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].status, ABCKIT_STATUS_NO_ERROR) << run.workers[index].firstFailedTask;
        ASSERT_EQ(run.workers[index].processedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].committedFunctions, EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].createdCheckBlocks, EXPECTED_FUNCTION_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].createdFailureBlocks, EXPECTED_FUNCTION_COUNT / workerCount);
    }
    auto parallelTotals = GetTransformTotals(run);
    ASSERT_EQ(parallelTotals.status, ABCKIT_STATUS_NO_ERROR) << parallelTotals.firstFailedTask;
    ASSERT_EQ(parallelTotals.processedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(parallelTotals.committedFunctions, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(parallelTotals.createdCheckBlocks, EXPECTED_FUNCTION_COUNT);
    ASSERT_EQ(parallelTotals.createdFailureBlocks, EXPECTED_FUNCTION_COUNT);
    g_impl->writeAbc(file.Get(), outputPath.c_str(), outputPath.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    ScopedAbckitFile reopenedFile(g_impl->openAbc(outputPath.c_str(), outputPath.size()));
    ASSERT_NE(reopenedFile.Get(), nullptr);
    auto persistedTasks = DiscoverTasks(reopenedFile.Get());
    ASSERT_EQ(persistedTasks.size(), EXPECTED_FUNCTION_COUNT);
    for (size_t index = 0; index < persistedTasks.size(); ++index) {
        EXPECT_EQ(TaskKey(persistedTasks[index]), expectedTasks[index]);
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(persistedTasks[index].function, observation));
        auto stats = GetGraphStats(persistedTasks[index].function);
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << TaskKey(persistedTasks[index]);
        EXPECT_GT(stats.arrayLoads, 0U) << TaskKey(persistedTasks[index]);
        EXPECT_EQ(stats.lengthLoads, 1U) << TaskKey(persistedTasks[index]);
        EXPECT_EQ(stats.greaterOrEqualChecks, 1U) << TaskKey(persistedTasks[index]);
        EXPECT_EQ(stats.branches, 1U) << TaskKey(persistedTasks[index]);
        EXPECT_EQ(stats.minusOneReturns, 1U) << TaskKey(persistedTasks[index]);
    }
    EXPECT_EQ(reopenedFile.Close(), ABCKIT_STATUS_NO_ERROR);

    if (verifyRuntimeOutput) {
        observation.output = helpers::ExecuteDynamicAbc(outputPath.c_str(), "parameter_check_parallel");
        ASSERT_EQ(observation.output, MODIFIED_OUTPUT);
    }
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitParallelParameterCheckScenarioTest, LibAbcKitTestParallelParameterCheckDynamic)
{
    scenario::Observation serial;
    scenario::Observation parallel;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT_PATH + std::string(".serial.abc"), serial));
    ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT_PATH, parallel));
    ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
}

}  // namespace libabckit::test
