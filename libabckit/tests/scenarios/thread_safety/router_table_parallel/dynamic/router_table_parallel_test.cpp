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

#include "router_table_instrumenter.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "scenarios/thread_safety/common/scenario_harness.h"

#include "helpers/helpers_runtime.h"

namespace libabckit::test {
namespace {

constexpr auto VERSION = ABCKIT_VERSION_RELEASE_1_0_0;
constexpr auto INPUT = ABCKIT_ABC_DIR "scenarios/thread_safety/router_table_parallel/dynamic/router_table_parallel.abc";
constexpr auto OUTPUT =
    ABCKIT_ABC_DIR "scenarios/thread_safety/router_table_parallel/dynamic/router_table_parallel_modified.abc";
constexpr std::string_view ROUTER_ANNOTATION_NAME = "Router";
constexpr std::string_view INITIALIZER_NAME = "func_main_0";
constexpr size_t WORKER_COUNT = 4;
constexpr size_t ROUTES_PER_DOMAIN = 2;
constexpr size_t EXPECTED_ROUTE_COUNT = WORKER_COUNT * ROUTES_PER_DOMAIN;

const AbckitApi *g_impl = AbckitGetApiImpl(VERSION);
const AbckitInspectApi *g_implI = AbckitGetInspectApiImpl(VERSION);
const AbckitGraphApi *g_implG = AbckitGetGraphApiImpl(VERSION);
const AbckitIsaApiDynamic *g_dynG = AbckitGetIsaApiDynamicImpl(VERSION);

struct ExpectedDomain {
    std::string_view domain;
    std::string_view routerModule;
    std::string_view handlerModule;
};

const std::array<ExpectedDomain, WORKER_COUNT> EXPECTED_DOMAINS = {
    ExpectedDomain {"order", "modules/order_router_map", "modules/order_handlers"},
    ExpectedDomain {"account", "modules/account_router_map", "modules/account_handlers"},
    ExpectedDomain {"payment", "modules/payment_router_map", "modules/payment_handlers"},
    ExpectedDomain {"delivery", "modules/delivery_router_map", "modules/delivery_handlers"},
};

struct ExpectedRoute {
    std::string_view className;
    std::string_view fullPath;
    std::string_view handlerLog;
};

const std::array<std::array<ExpectedRoute, ROUTES_PER_DOMAIN>, WORKER_COUNT> EXPECTED_ROUTES = {
    std::array<ExpectedRoute, ROUTES_PER_DOMAIN> {
        ExpectedRoute {"OrderCancelHandler", "app://order/cancel", "handler:order.cancel"},
        ExpectedRoute {"OrderCreateHandler", "app://order/create", "handler:order.create"},
    },
    std::array<ExpectedRoute, ROUTES_PER_DOMAIN> {
        ExpectedRoute {"AccountCloseHandler", "app://account/close", "handler:account.close"},
        ExpectedRoute {"AccountOpenHandler", "app://account/open", "handler:account.open"},
    },
    std::array<ExpectedRoute, ROUTES_PER_DOMAIN> {
        ExpectedRoute {"PaymentAuthorizeHandler", "app://payment/authorize", "handler:payment.authorize"},
        ExpectedRoute {"PaymentRefundHandler", "app://payment/refund", "handler:payment.refund"},
    },
    std::array<ExpectedRoute, ROUTES_PER_DOMAIN> {
        ExpectedRoute {"DeliveryCancelHandler", "app://delivery/cancel", "handler:delivery.cancel"},
        ExpectedRoute {"DeliveryScheduleHandler", "app://delivery/schedule", "handler:delivery.schedule"},
    },
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

std::string GetString(AbckitString *value, AbckitStatus &status)
{
    if (!RecordPointer(value, status)) {
        return {};
    }
    auto *data = g_implI->abckitStringToString(value);
    if (!RecordPointer(data, status)) {
        return {};
    }
    return data;
}

struct DiscoveryResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::array<RouterTableTask, WORKER_COUNT> tasks;
};

size_t FindDomain(std::string_view moduleName, bool routerModule)
{
    auto it = std::find_if(EXPECTED_DOMAINS.begin(), EXPECTED_DOMAINS.end(), [&](const auto &domain) {
        return moduleName == (routerModule ? domain.routerModule : domain.handlerModule);
    });
    return static_cast<size_t>(std::distance(EXPECTED_DOMAINS.begin(), it));
}

struct InitializerContext {
    DiscoveryResult *discovery = nullptr;
    size_t domainIndex = 0;
};

bool VisitInitializer(AbckitCoreFunction *function, void *data)
{
    auto &context = *static_cast<InitializerContext *>(data);
    auto name = GetString(g_implI->functionGetName(function), context.discovery->status);
    if (context.discovery->status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    if (name == INITIALIZER_NAME) {
        auto &task = context.discovery->tasks[context.domainIndex];
        if (task.initializer != nullptr) {
            SetFirstError(context.discovery->status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        task.initializer = function;
    }
    return true;
}

struct AnnotationElementContext {
    AbckitStatus *status = nullptr;
    std::string scheme;
    std::string path;
};

bool VisitAnnotationElement(AbckitCoreAnnotationElement *element, void *data)
{
    auto &context = *static_cast<AnnotationElementContext *>(data);
    auto name = GetString(g_implI->annotationElementGetName(element), *context.status);
    if (*context.status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *value = g_implI->annotationElementGetValue(element);
    if (!RecordPointer(value, *context.status)) {
        return false;
    }
    if (name == "scheme") {
        context.scheme = GetString(g_implI->valueGetString(value), *context.status);
    } else if (name == "path") {
        context.path = GetString(g_implI->valueGetString(value), *context.status);
    }
    return *context.status == ABCKIT_STATUS_NO_ERROR;
}

struct AnnotationContext {
    DiscoveryResult *discovery = nullptr;
    size_t domainIndex = 0;
    AbckitCoreModule *handlerModule = nullptr;
    AbckitCoreClass *handlerClass = nullptr;
    AbckitString *className = nullptr;
    std::string classNameText;
};

bool VisitAnnotation(AbckitCoreAnnotation *annotation, void *data)
{
    auto &context = *static_cast<AnnotationContext *>(data);
    auto *annotationInterface = g_implI->annotationGetInterface(annotation);
    if (!RecordPointer(annotationInterface, context.discovery->status)) {
        return false;
    }
    auto name = GetString(g_implI->annotationInterfaceGetName(annotationInterface), context.discovery->status);
    if (context.discovery->status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    if (name != ROUTER_ANNOTATION_NAME) {
        return true;
    }

    AnnotationElementContext elements {&context.discovery->status, {}, {}};
    auto visited = g_implI->annotationEnumerateElements(annotation, &elements, VisitAnnotationElement);
    if (!RecordLastError(context.discovery->status) || !visited || elements.scheme.empty() || elements.path.empty()) {
        SetFirstError(context.discovery->status, ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    context.discovery->tasks[context.domainIndex].entries.push_back(
        {context.handlerModule, context.handlerClass, annotation, context.className, context.classNameText,
         elements.scheme + elements.path, nullptr});
    return true;
}

struct ClassContext {
    DiscoveryResult *discovery = nullptr;
    size_t domainIndex = 0;
    AbckitCoreModule *handlerModule = nullptr;
};

bool VisitHandlerClass(AbckitCoreClass *klass, void *data)
{
    auto &context = *static_cast<ClassContext *>(data);
    auto *className = g_implI->classGetName(klass);
    auto classNameText = GetString(className, context.discovery->status);
    if (context.discovery->status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    AnnotationContext annotationContext {context.discovery, context.domainIndex, context.handlerModule, klass,
                                         className,         classNameText};
    auto visited = g_implI->classEnumerateAnnotations(klass, &annotationContext, VisitAnnotation);
    if (!RecordLastError(context.discovery->status) || !visited) {
        SetFirstError(context.discovery->status, ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    return true;
}

bool VisitModule(AbckitCoreModule *module, void *data)
{
    auto &discovery = *static_cast<DiscoveryResult *>(data);
    auto moduleName = GetString(g_implI->moduleGetName(module), discovery.status);
    if (discovery.status != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }

    auto domainIndex = FindDomain(moduleName, true);
    if (domainIndex != WORKER_COUNT) {
        auto &task = discovery.tasks[domainIndex];
        task.routerModule = module;
        InitializerContext context {&discovery, domainIndex};
        auto visited = g_implI->moduleEnumerateTopLevelFunctions(module, &context, VisitInitializer);
        if (!RecordLastError(discovery.status) || !visited) {
            SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        return true;
    }

    domainIndex = FindDomain(moduleName, false);
    if (domainIndex != WORKER_COUNT) {
        ClassContext context {&discovery, domainIndex, module};
        auto visited = g_implI->moduleEnumerateClasses(module, &context, VisitHandlerClass);
        if (!RecordLastError(discovery.status) || !visited) {
            SetFirstError(discovery.status, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
    }
    return true;
}

DiscoveryResult DiscoverRouterTables(AbckitFile *file)
{
    DiscoveryResult result;
    for (size_t index = 0; index < WORKER_COUNT; ++index) {
        result.tasks[index].domain = EXPECTED_DOMAINS[index].domain;
        result.tasks[index].moduleName = EXPECTED_DOMAINS[index].routerModule;
    }
    auto visited = g_implI->fileEnumerateModules(file, &result, VisitModule);
    if (!RecordLastError(result.status) || !visited) {
        SetFirstError(result.status, ABCKIT_STATUS_INTERNAL_ERROR);
    }
    for (auto &task : result.tasks) {
        std::sort(task.entries.begin(), task.entries.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.fullPath < rhs.fullPath; });
    }
    return result;
}

struct GraphStats {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t emptyArrays = 0;
    size_t routeArrays = 0;
    size_t externalLoads = 0;
    size_t holeChecks = 0;
    size_t newObjects = 0;
    size_t indexStores = 0;
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
        switch (opcode) {
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_CREATEEMPTYARRAY:
                ++stats.emptyArrays;
                break;
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_CREATEARRAYWITHBUFFER:
                ++stats.routeArrays;
                break;
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR:
                ++stats.externalLoads;
                break;
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_THROW_UNDEFINEDIFHOLEWITHNAME:
                ++stats.holeChecks;
                break;
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE:
                ++stats.newObjects;
                break;
            case ABCKIT_ISA_API_DYNAMIC_OPCODE_STOWNBYINDEX:
                ++stats.indexStores;
                break;
            default:
                break;
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

void MergeResult(RouterTableTransformResult &target, const RouterTableTransformResult &source)
{
    SetFirstError(target.status, source.status);
    target.importsAdded += source.importsAdded;
    target.annotationsRemoved += source.annotationsRemoved;
    target.tablesProcessed += source.tablesProcessed;
    target.routesInserted += source.routesInserted;
    target.tablesCommitted += source.tablesCommitted;
}

struct WorkerResult : RouterTableTransformResult {
    std::string firstFailedDomain;
};

WorkerResult TransformShard(std::array<RouterTableTask, WORKER_COUNT> &tasks, const std::vector<size_t> &shard,
                            bool metadataOnly)
{
    WorkerResult worker;
    RouterTableInstrumenter instrumenter(VERSION);
    for (auto index : shard) {
        auto result = metadataOnly ? instrumenter.PrepareMetadata(tasks[index]) : instrumenter.BuildTable(tasks[index]);
        MergeResult(worker, result);
        if (worker.status != ABCKIT_STATUS_NO_ERROR) {
            worker.firstFailedDomain = tasks[index].domain;
            break;
        }
    }
    return worker;
}

using TransformRun = scenario::RunResult<WorkerResult>;

TransformRun TransformTables(std::array<RouterTableTask, WORKER_COUNT> &tasks, size_t workerCount)
{
    // Finish and join all structural metadata work before any worker creates a Graph.
    auto metadata = scenario::RunShards<WorkerResult>(
        tasks.size(), workerCount, [&](const auto &shard) { return TransformShard(tasks, shard, true); });
    if (metadata.status != ABCKIT_STATUS_NO_ERROR) {
        return metadata;
    }
    auto run = scenario::RunShards<WorkerResult>(
        tasks.size(), workerCount, [&](const auto &shard) { return TransformShard(tasks, shard, false); });
    for (size_t index = 0; index < workerCount; ++index) {
        MergeResult(run.workers[index], metadata.workers[index]);
    }
    return run;
}

size_t CountRoutes(const std::array<RouterTableTask, WORKER_COUNT> &tasks)
{
    size_t count = 0;
    for (const auto &task : tasks) {
        count += task.entries.size();
    }
    return count;
}

std::string GetModifiedOutput()
{
    std::ostringstream output;
    for (size_t domainIndex = 0; domainIndex < EXPECTED_DOMAINS.size(); ++domainIndex) {
        for (const auto &route : EXPECTED_ROUTES[domainIndex]) {
            output << "dispatch:" << EXPECTED_DOMAINS[domainIndex].domain << ':' << route.fullPath << '\n';
            output << route.handlerLog << '\n';
        }
    }
    return output.str();
}

using scenario::ShouldVerifyRuntimeOutput;

}  // namespace

class AbckitParallelRouterTableScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        ASSERT_EQ(helpers::ExecuteDynamicAbc(INPUT, "router_table_parallel"), "");
    }

    ScopedAbckitFile file(g_impl->openAbc(INPUT, std::char_traits<char>::length(INPUT)));
    ASSERT_NE(file.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto discovery = DiscoverRouterTables(file.Get());
    ASSERT_EQ(discovery.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(CountRoutes(discovery.tasks), EXPECTED_ROUTE_COUNT);
    std::array<GraphStats, WORKER_COUNT> originalStats;
    for (size_t domainIndex = 0; domainIndex < WORKER_COUNT; ++domainIndex) {
        const auto &task = discovery.tasks[domainIndex];
        ASSERT_NE(task.routerModule, nullptr) << task.domain;
        ASSERT_NE(task.initializer, nullptr) << task.domain;
        ASSERT_EQ(task.moduleName, EXPECTED_DOMAINS[domainIndex].routerModule);
        ASSERT_EQ(task.entries.size(), ROUTES_PER_DOMAIN) << task.domain;
        for (size_t routeIndex = 0; routeIndex < ROUTES_PER_DOMAIN; ++routeIndex) {
            const auto &entry = task.entries[routeIndex];
            ASSERT_EQ(entry.classNameText, EXPECTED_ROUTES[domainIndex][routeIndex].className);
            ASSERT_EQ(entry.fullPath, EXPECTED_ROUTES[domainIndex][routeIndex].fullPath);
            ASSERT_NE(entry.handlerModule, nullptr);
            ASSERT_NE(entry.handlerClass, nullptr);
            ASSERT_NE(entry.annotation, nullptr);
            ASSERT_NE(entry.className, nullptr);
        }
        originalStats[domainIndex] = GetGraphStats(task.initializer);
        ASSERT_EQ(originalStats[domainIndex].status, ABCKIT_STATUS_NO_ERROR) << task.domain;
        ASSERT_EQ(originalStats[domainIndex].emptyArrays, 1U) << task.domain;
        ASSERT_EQ(originalStats[domainIndex].routeArrays, 0U) << task.domain;
        ASSERT_EQ(originalStats[domainIndex].indexStores, 0U) << task.domain;
    }

    auto run = TransformTables(discovery.tasks, workerCount);
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    size_t importsAdded = 0;
    size_t annotationsRemoved = 0;
    size_t tablesProcessed = 0;
    size_t routesInserted = 0;
    size_t tablesCommitted = 0;
    for (size_t index = 0; index < workerCount; ++index) {
        ASSERT_EQ(run.shardSizes[index], WORKER_COUNT / workerCount);
        ASSERT_EQ(run.workers[index].status, ABCKIT_STATUS_NO_ERROR) << run.workers[index].firstFailedDomain;
        EXPECT_EQ(run.workers[index].importsAdded, EXPECTED_ROUTE_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].annotationsRemoved, EXPECTED_ROUTE_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].tablesProcessed, WORKER_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].routesInserted, EXPECTED_ROUTE_COUNT / workerCount);
        EXPECT_EQ(run.workers[index].tablesCommitted, WORKER_COUNT / workerCount);
        importsAdded += run.workers[index].importsAdded;
        annotationsRemoved += run.workers[index].annotationsRemoved;
        tablesProcessed += run.workers[index].tablesProcessed;
        routesInserted += run.workers[index].routesInserted;
        tablesCommitted += run.workers[index].tablesCommitted;
    }
    ASSERT_EQ(importsAdded, EXPECTED_ROUTE_COUNT);
    ASSERT_EQ(annotationsRemoved, EXPECTED_ROUTE_COUNT);
    ASSERT_EQ(tablesProcessed, WORKER_COUNT);
    ASSERT_EQ(routesInserted, EXPECTED_ROUTE_COUNT);
    ASSERT_EQ(tablesCommitted, WORKER_COUNT);

    auto withoutAnnotations = DiscoverRouterTables(file.Get());
    ASSERT_EQ(withoutAnnotations.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(CountRoutes(withoutAnnotations.tasks), 0U);

    g_impl->writeAbc(file.Get(), outputPath.c_str(), outputPath.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
    if (verifyRuntimeOutput) {
        observation.output = helpers::ExecuteDynamicAbc(outputPath.c_str(), "router_table_parallel");
        ASSERT_EQ(observation.output, GetModifiedOutput());
    }

    ScopedAbckitFile reopened(g_impl->openAbc(outputPath.c_str(), outputPath.size()));
    ASSERT_NE(reopened.Get(), nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto persisted = DiscoverRouterTables(reopened.Get());
    ASSERT_EQ(persisted.status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(CountRoutes(persisted.tasks), 0U);
    for (size_t domainIndex = 0; domainIndex < WORKER_COUNT; ++domainIndex) {
        const auto &task = persisted.tasks[domainIndex];
        ASSERT_NE(task.routerModule, nullptr) << task.domain;
        ASSERT_NE(task.initializer, nullptr) << task.domain;
        auto stats = GetGraphStats(task.initializer);
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(task.initializer, observation));
        ASSERT_EQ(stats.status, ABCKIT_STATUS_NO_ERROR) << task.domain;
        EXPECT_EQ(stats.emptyArrays, originalStats[domainIndex].emptyArrays) << task.domain;
        EXPECT_EQ(stats.routeArrays, originalStats[domainIndex].routeArrays + ROUTES_PER_DOMAIN) << task.domain;
        EXPECT_EQ(stats.externalLoads, originalStats[domainIndex].externalLoads + ROUTES_PER_DOMAIN) << task.domain;
        EXPECT_EQ(stats.holeChecks, originalStats[domainIndex].holeChecks + ROUTES_PER_DOMAIN) << task.domain;
        EXPECT_EQ(stats.newObjects, originalStats[domainIndex].newObjects + ROUTES_PER_DOMAIN) << task.domain;
        EXPECT_EQ(stats.indexStores, originalStats[domainIndex].indexStores + 2U * ROUTES_PER_DOMAIN) << task.domain;
    }
    ASSERT_EQ(reopened.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitParallelRouterTableScenarioTest, LibAbcKitTestParallelRouterTableDynamic)
{
    scenario::Observation serial;
    scenario::Observation parallel;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT + std::string(".serial.abc"), serial));
    ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT, parallel));
    ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
}

}  // namespace libabckit::test
