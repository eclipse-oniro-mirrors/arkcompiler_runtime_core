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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "scenarios/thread_safety/common/scenario_harness.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "helpers/visit_helper/visit_helper-inl.h"
#include "helpers/visit_helper/visit_helper.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/arkts/metadata_arkts.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test {
namespace {

auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implArkI = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implArkM = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

constexpr std::string_view API_CLASS_NAME = "ApiControl";
constexpr std::string_view API_METHOD_NAME = "fixOrientationLinearLayout";
constexpr std::string_view API_MODULE_NAME = "modules/ApiControl";
constexpr std::string_view FEATURE_ALPHA_MODULE_NAME = "modules/FeatureAlpha";
constexpr std::string_view FEATURE_BETA_MODULE_NAME = "modules/FeatureBeta";
constexpr std::string_view ANNOTATION_INTERFACE_NAME = "CallSiteReplacement";
constexpr std::string_view TARGET_CLASS_NAME = "LegacyLayout";
constexpr std::string_view TARGET_METHOD_NAME = "setOrientation";
constexpr size_t WORKER_COUNT = 4;
constexpr int32_t REPLACEMENT_VALUE = 5;

const std::unordered_map<std::string, size_t> EXPECTED_CALL_SITES = {
    {"updateHeader", 1}, {"updateContent", 2}, {"updateSidebar", 1}, {"updateFooter", 1}};

struct ReplacementMetadata {
    AbckitString *targetClass = nullptr;
    AbckitString *targetMethod = nullptr;
    AbckitString *replacementClassName = nullptr;
    AbckitString *replacementMethodName = nullptr;
    AbckitCoreClass *replacementClass = nullptr;
    AbckitCoreFunction *replacementMethod = nullptr;
};

struct FunctionTask {
    std::string name;
    AbckitCoreFunction *function = nullptr;
    AbckitCoreModule *module = nullptr;
    AbckitCoreImportDescriptor *apiImport = nullptr;
    size_t expectedCallSites = 0;
};

struct WorkerResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t functionsProcessed = 0;
    size_t callSitesReplaced = 0;
};

struct TransformContext {
    AbckitGraph *graph = nullptr;
    AbckitCoreImportDescriptor *apiImport = nullptr;
    AbckitString *targetMethodName = nullptr;
    AbckitString *replacementClassName = nullptr;
    AbckitString *replacementMethodName = nullptr;
    AbckitInst *layoutInstance = nullptr;
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t replacements = 0;
};

struct VerificationContext {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t replacements = 0;
    size_t originalTargetCalls = 0;
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
    auto error = g_impl->getLastError();
    SetFirstError(status, error);
    return error == ABCKIT_STATUS_NO_ERROR;
}

template <typename T>
bool RecordPointerResult(T *value, AbckitStatus &status)
{
    if (!RecordLastError(status)) {
        return false;
    }
    if (value == nullptr) {
        SetFirstError(status, ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    return true;
}

bool StringEquals(AbckitString *actual, AbckitString *expected, AbckitStatus &status)
{
    auto *actualValue = g_implI->abckitStringToString(actual);
    if (!RecordPointerResult(actualValue, status)) {
        return false;
    }
    auto *expectedValue = g_implI->abckitStringToString(expected);
    if (!RecordPointerResult(expectedValue, status)) {
        return false;
    }
    return std::string_view(actualValue) == expectedValue;
}

bool StringEquals(AbckitString *actual, std::string_view expected, AbckitStatus &status)
{
    auto *actualValue = g_implI->abckitStringToString(actual);
    return RecordPointerResult(actualValue, status) && std::string_view(actualValue) == expected;
}

bool IsTargetCall(AbckitInst *inst, TransformContext &ctx)
{
    auto opcode = g_dynG->iGetOpcode(inst);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1 &&
        opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1WITHNAME) {
        return false;
    }

    auto inputCount = g_implG->iGetInputCount(inst);
    if (!RecordLastError(ctx.status) || inputCount < 2) {
        return false;
    }
    auto *thisObject = g_implG->iGetInput(inst, 1);
    if (!RecordPointerResult(thisObject, ctx.status) || thisObject != ctx.layoutInstance) {
        return false;
    }

    auto *callee = g_implG->iGetInput(inst, 0);
    if (!RecordPointerResult(callee, ctx.status)) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(callee);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
        return false;
    }
    auto *methodName = g_implG->iGetString(callee);
    return RecordPointerResult(methodName, ctx.status) && StringEquals(methodName, ctx.targetMethodName, ctx.status);
}

bool InsertReplacementCall(AbckitInst *call, TransformContext &ctx)
{
    auto *ldExternal = g_dynG->iCreateLdexternalmodulevar(ctx.graph, ctx.apiImport);
    if (!RecordPointerResult(ldExternal, ctx.status)) {
        return false;
    }
    auto *classThrow = g_dynG->iCreateThrowUndefinedifholewithname(ctx.graph, ldExternal, ctx.replacementClassName);
    if (!RecordPointerResult(classThrow, ctx.status)) {
        return false;
    }
    auto *replacementValue = g_implG->gFindOrCreateConstantI32(ctx.graph, REPLACEMENT_VALUE);
    if (!RecordPointerResult(replacementValue, ctx.status)) {
        return false;
    }
    auto *ldMethod = g_dynG->iCreateLdobjbyname(ctx.graph, ldExternal, ctx.replacementMethodName);
    if (!RecordPointerResult(ldMethod, ctx.status)) {
        return false;
    }
    auto *replacementCall =
        g_dynG->iCreateCallthis2(ctx.graph, ldMethod, ldExternal, ctx.layoutInstance, replacementValue);
    if (!RecordPointerResult(replacementCall, ctx.status)) {
        return false;
    }

    g_implG->iInsertAfter(ldExternal, call);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    g_implG->iInsertAfter(classThrow, ldExternal);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    g_implG->iInsertAfter(ldMethod, classThrow);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    g_implG->iInsertAfter(replacementCall, ldMethod);
    if (!RecordLastError(ctx.status)) {
        return false;
    }

    ++ctx.replacements;
    return true;
}

bool TransformBlock(AbckitBasicBlock *basicBlock, void *data)
{
    auto &ctx = *static_cast<TransformContext *>(data);
    auto *inst = g_implG->bbGetFirstInst(basicBlock);
    if (!RecordLastError(ctx.status)) {
        return false;
    }

    while (inst != nullptr) {
        // Preserve the original successor because the replacement sequence is inserted after inst.
        auto *next = g_implG->iGetNext(inst);
        if (!RecordLastError(ctx.status)) {
            return false;
        }
        if (IsTargetCall(inst, ctx) && !InsertReplacementCall(inst, ctx)) {
            return false;
        }
        if (ctx.status != ABCKIT_STATUS_NO_ERROR) {
            return false;
        }
        inst = next;
    }
    return true;
}

WorkerResult TransformShard(const std::vector<size_t> &shard, const std::vector<FunctionTask> &tasks,
                            const ReplacementMetadata &metadata)
{
    WorkerResult result;
    for (auto taskIndex : shard) {
        const auto &task = tasks[taskIndex];
        auto *graph = g_implI->createGraphFromFunction(task.function);
        if (!RecordPointerResult(graph, result.status)) {
            break;
        }

        TransformContext ctx {graph,
                              task.apiImport,
                              metadata.targetMethod,
                              metadata.replacementClassName,
                              metadata.replacementMethodName,
                              helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE)};
        if (!RecordPointerResult(ctx.layoutInstance, ctx.status)) {
            SetFirstError(result.status, ctx.status);
        } else {
            auto visitedAllBlocks = g_implG->gVisitBlocksRpo(graph, &ctx, TransformBlock);
            RecordLastError(ctx.status);
            if (!visitedAllBlocks && ctx.status == ABCKIT_STATUS_NO_ERROR) {
                ctx.status = ABCKIT_STATUS_INTERNAL_ERROR;
            }
            if (ctx.status == ABCKIT_STATUS_NO_ERROR && ctx.replacements != task.expectedCallSites) {
                ctx.status = ABCKIT_STATUS_INTERNAL_ERROR;
            }
            SetFirstError(result.status, ctx.status);
        }

        if (result.status == ABCKIT_STATUS_NO_ERROR) {
            g_implM->functionSetGraph(task.function, graph);
            RecordLastError(result.status);
        }

        g_impl->destroyGraph(graph);
        RecordLastError(result.status);
        if (result.status != ABCKIT_STATUS_NO_ERROR) {
            break;
        }

        ++result.functionsProcessed;
        result.callSitesReplaced += ctx.replacements;
    }
    return result;
}

bool IsPersistedReplacement(AbckitInst *inst, VerificationContext &ctx)
{
    auto opcode = g_dynG->iGetOpcode(inst);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS2) {
        return false;
    }

    auto inputCount = g_implG->iGetInputCount(inst);
    if (!RecordLastError(ctx.status) || inputCount < 4) {
        return false;
    }
    auto *ldMethod = g_implG->iGetInput(inst, 0);
    if (!RecordPointerResult(ldMethod, ctx.status)) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(ldMethod);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
        return false;
    }

    auto *methodName = g_implG->iGetString(ldMethod);
    if (!RecordPointerResult(methodName, ctx.status)) {
        return false;
    }
    if (!StringEquals(methodName, API_METHOD_NAME, ctx.status)) {
        return false;
    }

    auto *ldExternal = g_implG->iGetInput(ldMethod, 0);
    if (!RecordPointerResult(ldExternal, ctx.status)) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(ldExternal);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
        return false;
    }

    auto *callThis = g_implG->iGetInput(inst, 1);
    auto *layoutInstance = g_implG->iGetInput(inst, 2);
    auto *replacementValue = g_implG->iGetInput(inst, 3);
    if (!RecordPointerResult(callThis, ctx.status) || !RecordPointerResult(layoutInstance, ctx.status) ||
        !RecordPointerResult(replacementValue, ctx.status) || callThis != ldExternal) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(layoutInstance);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(replacementValue);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT) {
        return false;
    }
    auto constantValue = g_implG->iGetConstantValueI32(replacementValue);
    if (!RecordLastError(ctx.status) || constantValue != REPLACEMENT_VALUE) {
        return false;
    }

    auto *classThrow = g_implG->iGetPrev(ldMethod);
    if (!RecordPointerResult(classThrow, ctx.status)) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(classThrow);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_THROW_UNDEFINEDIFHOLEWITHNAME) {
        return false;
    }
    auto *className = g_implG->iGetString(classThrow);
    if (!RecordPointerResult(className, ctx.status) || !StringEquals(className, API_CLASS_NAME, ctx.status)) {
        return false;
    }
    auto *checkedClass = g_implG->iGetInput(classThrow, 0);
    if (!RecordPointerResult(checkedClass, ctx.status) || checkedClass != ldExternal) {
        return false;
    }
    auto *previous = g_implG->iGetPrev(classThrow);
    if (!RecordPointerResult(previous, ctx.status) || previous != ldExternal) {
        return false;
    }
    auto *next = g_implG->iGetNext(ldMethod);
    if (!RecordPointerResult(next, ctx.status) || next != inst) {
        return false;
    }

    auto *importDescriptor = g_dynG->iGetImportDescriptor(ldExternal);
    if (!RecordPointerResult(importDescriptor, ctx.status)) {
        return false;
    }
    auto *importName = g_implI->importDescriptorGetName(importDescriptor);
    if (!RecordPointerResult(importName, ctx.status)) {
        return false;
    }
    auto *name = g_implI->abckitStringToString(importName);
    if (!RecordPointerResult(name, ctx.status)) {
        return false;
    }
    return std::string_view(name) == API_CLASS_NAME;
}

bool IsPersistedOriginalTargetCall(AbckitInst *inst, VerificationContext &ctx)
{
    auto opcode = g_dynG->iGetOpcode(inst);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1 &&
        opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1WITHNAME) {
        return false;
    }
    auto inputCount = g_implG->iGetInputCount(inst);
    if (!RecordLastError(ctx.status) || inputCount < 2) {
        return false;
    }
    auto *callee = g_implG->iGetInput(inst, 0);
    auto *receiver = g_implG->iGetInput(inst, 1);
    if (!RecordPointerResult(callee, ctx.status) || !RecordPointerResult(receiver, ctx.status)) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(callee);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
        return false;
    }
    opcode = g_dynG->iGetOpcode(receiver);
    if (!RecordLastError(ctx.status) || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE) {
        return false;
    }
    auto *methodName = g_implG->iGetString(callee);
    return RecordPointerResult(methodName, ctx.status) && StringEquals(methodName, TARGET_METHOD_NAME, ctx.status);
}

bool VerifyBlock(AbckitBasicBlock *basicBlock, void *data)
{
    auto &ctx = *static_cast<VerificationContext *>(data);
    auto *inst = g_implG->bbGetFirstInst(basicBlock);
    if (!RecordLastError(ctx.status)) {
        return false;
    }
    while (inst != nullptr) {
        auto *next = g_implG->iGetNext(inst);
        if (!RecordLastError(ctx.status)) {
            return false;
        }
        if (IsPersistedReplacement(inst, ctx)) {
            ++ctx.replacements;
        }
        if (IsPersistedOriginalTargetCall(inst, ctx)) {
            ++ctx.originalTargetCalls;
        }
        if (ctx.status != ABCKIT_STATUS_NO_ERROR) {
            return false;
        }
        inst = next;
    }
    return true;
}

ReplacementMetadata DiscoverReplacementMetadata(VisitHelper &visitor, AbckitCoreModule *apiModule)
{
    ReplacementMetadata metadata;
    visitor.EnumerateModuleClasses(apiModule, [&](AbckitCoreClass *klass) {
        visitor.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
            visitor.EnumerateMethodAnnotations(method, [&](AbckitCoreAnnotation *annotation) {
                auto *annotationInterface = g_implI->annotationGetInterface(annotation);
                if (visitor.GetString(g_implI->annotationInterfaceGetName(annotationInterface)) !=
                    ANNOTATION_INTERFACE_NAME) {
                    return;
                }
                visitor.EnumerateAnnotationElements(annotation, [&](AbckitCoreAnnotationElement *element) {
                    auto elementName = visitor.GetString(g_implI->annotationElementGetName(element));
                    auto *value = g_implI->annotationElementGetValue(element);
                    if (elementName == "targetClass") {
                        metadata.targetClass = g_implI->valueGetString(value);
                    } else if (elementName == "methodName") {
                        metadata.targetMethod = g_implI->valueGetString(value);
                    }
                });
                metadata.replacementClass = klass;
                metadata.replacementMethod = method;
            });
        });
    });
    if (metadata.replacementClass != nullptr) {
        metadata.replacementClassName = g_implI->classGetName(metadata.replacementClass);
    }
    if (metadata.replacementMethod != nullptr) {
        metadata.replacementMethodName = g_implI->functionGetName(metadata.replacementMethod);
    }
    return metadata;
}

std::vector<FunctionTask> DiscoverFunctionTasks(VisitHelper &visitor,
                                                const std::array<AbckitCoreModule *, 2> &featureModules)
{
    std::vector<FunctionTask> tasks;
    for (auto *module : featureModules) {
        visitor.EnumerateModuleClasses(module, [&](AbckitCoreClass *klass) {
            visitor.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
                auto name = std::string(visitor.GetString(g_implI->functionGetName(method)));
                auto expected = EXPECTED_CALL_SITES.find(name);
                if (expected != EXPECTED_CALL_SITES.end()) {
                    tasks.push_back({name, method, module, nullptr, expected->second});
                }
            });
        });
    }
    std::sort(tasks.begin(), tasks.end(), [](const auto &lhs, const auto &rhs) { return lhs.name < rhs.name; });
    return tasks;
}

AbckitCoreImportDescriptor *AddApiImport(AbckitCoreModule *module, AbckitCoreModule *apiModule)
{
    AbckitArktsImportFromDynamicModuleCreateParams params {};
    params.name = API_CLASS_NAME.data();
    params.alias = API_CLASS_NAME.data();
    auto *arktsModule = g_implArkI->coreModuleToArktsModule(module);
    if (arktsModule == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return nullptr;
    }
    auto *arktsApiModule = g_implArkI->coreModuleToArktsModule(apiModule);
    if (arktsApiModule == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return nullptr;
    }
    auto *arktsImport = g_implArkM->moduleAddImportFromArktsV1ToArktsV1(arktsModule, arktsApiModule, &params);
    if (arktsImport == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return nullptr;
    }
    return g_implArkI->arktsImportDescriptorToCoreImportDescriptor(arktsImport);
}

std::vector<FunctionTask> DiscoverPersistedFunctions(VisitHelper &visitor,
                                                     const std::array<AbckitCoreModule *, 2> &featureModules)
{
    return DiscoverFunctionTasks(visitor, featureModules);
}

void VerifyPersistedGraphs(const std::vector<FunctionTask> &tasks)
{
    size_t totalReplacements = 0;
    for (const auto &task : tasks) {
        auto *graph = g_implI->createGraphFromFunction(task.function);
        ASSERT_NE(graph, nullptr) << task.name;
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR) << task.name;

        VerificationContext context;
        ASSERT_TRUE(g_implG->gVisitBlocksRpo(graph, &context, VerifyBlock)) << task.name;
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR) << task.name;
        ASSERT_EQ(context.status, ABCKIT_STATUS_NO_ERROR) << task.name;
        EXPECT_EQ(context.replacements, task.expectedCallSites) << task.name;
        // This Scenario follows the established call-site remediation semantics: keep the original call and
        // instrument a corrective API call immediately after it.
        EXPECT_EQ(context.originalTargetCalls, task.expectedCallSites) << task.name;
        totalReplacements += context.replacements;

        g_impl->destroyGraph(graph);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR) << task.name;
    }
    EXPECT_EQ(totalReplacements, 5U);
}

using scenario::ShouldVerifyRuntimeOutput;

}  // namespace

class AbckitThreadSafetyScenarioTest : public ::testing::Test {};

static void RunRewrite(size_t workerCount, const std::string &outputPath, scenario::Observation &observation)
{
    SCOPED_TRACE("workerCount=" + std::to_string(workerCount));
    constexpr auto INPUT_PATH =
        ABCKIT_ABC_DIR "scenarios/thread_safety/replace_call_site_parallel/dynamic/replace_call_site_parallel.abc";

    const bool verifyRuntimeOutput = ShouldVerifyRuntimeOutput();
    if (verifyRuntimeOutput) {
        const auto output = helpers::ExecuteDynamicAbc(INPUT_PATH, "replace_call_site_parallel");
        ASSERT_TRUE(helpers::Match(output,
                                   "11\n"
                                   "13\n"
                                   "21\n"
                                   "22\n"))
            << output;
    }

    auto *file = g_impl->openAbc(INPUT_PATH, std::char_traits<char>::length(INPUT_PATH));
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ScopedAbckitFile scopedFile(file);
    auto visitor = VisitHelper(file, g_impl, g_implI, g_implG, g_dynG);

    AbckitCoreModule *apiModule = nullptr;
    std::array<AbckitCoreModule *, 2> featureModules {};
    visitor.EnumerateModules([&](AbckitCoreModule *module) {
        auto moduleName = visitor.GetString(g_implI->moduleGetName(module));
        if (moduleName == API_MODULE_NAME) {
            apiModule = module;
        } else if (moduleName == FEATURE_ALPHA_MODULE_NAME) {
            featureModules[0] = module;
        } else if (moduleName == FEATURE_BETA_MODULE_NAME) {
            featureModules[1] = module;
        }
    });
    ASSERT_NE(apiModule, nullptr);
    ASSERT_NE(featureModules[0], nullptr);
    ASSERT_NE(featureModules[1], nullptr);

    auto metadata = DiscoverReplacementMetadata(visitor, apiModule);
    ASSERT_NE(metadata.targetClass, nullptr);
    ASSERT_NE(metadata.targetMethod, nullptr);
    ASSERT_NE(metadata.replacementClassName, nullptr);
    ASSERT_NE(metadata.replacementMethodName, nullptr);
    ASSERT_EQ(visitor.GetString(metadata.targetClass), TARGET_CLASS_NAME);
    ASSERT_EQ(visitor.GetString(metadata.targetMethod), TARGET_METHOD_NAME);
    ASSERT_EQ(visitor.GetString(metadata.replacementClassName), API_CLASS_NAME);
    ASSERT_EQ(visitor.GetString(metadata.replacementMethodName), API_METHOD_NAME);

    auto tasks = DiscoverFunctionTasks(visitor, featureModules);
    ASSERT_EQ(tasks.size(), WORKER_COUNT);
    ASSERT_TRUE(std::any_of(tasks.begin(), tasks.end(), [](const auto &task) { return task.expectedCallSites > 1; }));

    std::array<AbckitCoreImportDescriptor *, 2> imports {};
    for (size_t i = 0; i < featureModules.size(); ++i) {
        imports[i] = AddApiImport(featureModules[i], apiModule);
        ASSERT_NE(imports[i], nullptr);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    for (auto &task : tasks) {
        task.apiImport = task.module == featureModules[0] ? imports[0] : imports[1];
    }

    auto run = scenario::RunShards<WorkerResult>(
        tasks.size(), workerCount, [&](const auto &shard) { return TransformShard(shard, tasks, metadata); });
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    const auto &results = run.workers;

    size_t processedFunctions = 0;
    size_t replacedCallSites = 0;
    for (const auto &result : results) {
        ASSERT_EQ(result.status, ABCKIT_STATUS_NO_ERROR);
        ASSERT_GT(result.functionsProcessed, 0U);
        processedFunctions += result.functionsProcessed;
        replacedCallSites += result.callSitesReplaced;
    }
    ASSERT_EQ(processedFunctions, tasks.size());
    ASSERT_EQ(replacedCallSites, 5U);

    g_impl->writeAbc(file, outputPath.c_str(), outputPath.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(scopedFile.Close(), ABCKIT_STATUS_NO_ERROR);

    if (verifyRuntimeOutput) {
        const auto output = helpers::ExecuteDynamicAbc(outputPath.c_str(), "replace_call_site_parallel");
        observation.output = output;
        ASSERT_TRUE(helpers::Match(output,
                                   "fixOrientationLinearLayout was called\n"
                                   "5\n"
                                   "fixOrientationLinearLayout was called\n"
                                   "fixOrientationLinearLayout was called\n"
                                   "5\n"
                                   "fixOrientationLinearLayout was called\n"
                                   "5\n"
                                   "fixOrientationLinearLayout was called\n"
                                   "5\n"));
    }

    auto *persistedFile = g_impl->openAbc(outputPath.c_str(), outputPath.size());
    ASSERT_NE(persistedFile, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ScopedAbckitFile scopedPersistedFile(persistedFile);
    auto persistedVisitor = VisitHelper(persistedFile, g_impl, g_implI, g_implG, g_dynG);

    std::array<AbckitCoreModule *, 2> persistedFeatureModules {};
    persistedVisitor.EnumerateModules([&](AbckitCoreModule *module) {
        auto moduleName = persistedVisitor.GetString(g_implI->moduleGetName(module));
        if (moduleName == FEATURE_ALPHA_MODULE_NAME) {
            persistedFeatureModules[0] = module;
        } else if (moduleName == FEATURE_BETA_MODULE_NAME) {
            persistedFeatureModules[1] = module;
        }
    });
    ASSERT_NE(persistedFeatureModules[0], nullptr);
    ASSERT_NE(persistedFeatureModules[1], nullptr);

    auto persistedTasks = DiscoverPersistedFunctions(persistedVisitor, persistedFeatureModules);
    ASSERT_EQ(persistedTasks.size(), WORKER_COUNT);
    for (const auto &task : persistedTasks) {
        ASSERT_NO_FATAL_FAILURE(scenario::CaptureGraph(task.function, observation));
    }
    VerifyPersistedGraphs(persistedTasks);

    ASSERT_EQ(scopedPersistedFile.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c, threads=1/4
TEST_F(AbckitThreadSafetyScenarioTest, LibAbcKitTestDynamicParallelReplaceCallSite)
{
    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR
        "scenarios/thread_safety/replace_call_site_parallel/dynamic/replace_call_site_parallel_modified.abc";
    scenario::Observation serial;
    scenario::Observation parallel;
    ASSERT_NO_FATAL_FAILURE(RunRewrite(1, OUTPUT_PATH + std::string(".serial.abc"), serial));
    ASSERT_NO_FATAL_FAILURE(RunRewrite(WORKER_COUNT, OUTPUT_PATH, parallel));
    ASSERT_NO_FATAL_FAILURE(scenario::Compare(serial, parallel));
}

}  // namespace libabckit::test
