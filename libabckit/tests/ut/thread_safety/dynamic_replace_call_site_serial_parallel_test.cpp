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

// 20260807927972 Task 02-08: replace_call_site, serial versus four workers on distinct functions.
// Each worker owns its graph; committed graphs are destroyed before file persistence.

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/extensions/arkts/metadata_arkts.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "helpers/visit_helper/visit_helper-inl.h"
#include "helpers/visit_helper/visit_helper.h"
#include "libabckit/src/metadata_inspect_impl.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implArkI = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implArkM = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/thread_safety/replace_call_site_parallel/replace_call_site_parallel.abc";
constexpr auto SERIAL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/replace_call_site_parallel/replace_call_site_parallel_serial.abc";
constexpr auto PARALLEL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/replace_call_site_parallel/replace_call_site_parallel_parallel.abc";
constexpr auto ENTRY = "replace_call_site_parallel";

constexpr auto API_CLASS_NAME = "ApiControl";
constexpr auto API_MODULE_NAME = "modules/ApiControl";
constexpr auto MAIN_MODULE_NAME = "replace_call_site_parallel";
constexpr auto ANNOTATION_INTERFACE_NAME = "CallSiteReplacement";

struct UserData {
    AbckitString *targetClass = nullptr;
    AbckitString *methodName = nullptr;
    AbckitCoreClass *classToReplace = nullptr;
    AbckitCoreFunction *methodToReplace = nullptr;
};

static bool HasSearchedMethod(VisitHelper &visitor, AbckitCoreFunction *method, UserData &userData)
{
    bool isFind = false;
    visitor.EnumerateFunctionInsts(method, [&](AbckitInst *inst) {
        if (g_dynG->iGetOpcode(inst) != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
            return;
        }
        bool findNew = false;
        visitor.EnumerateInstUsers(inst, [&](AbckitInst *user) {
            if (g_dynG->iGetOpcode(user) == ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE) {
                findNew = true;
            }
        });
        visitor.EnumerateInstUsers(inst, [&](AbckitInst *user) {
            if (findNew && !isFind &&
                g_dynG->iGetOpcode(user) == ABCKIT_ISA_API_DYNAMIC_OPCODE_THROW_UNDEFINEDIFHOLEWITHNAME) {
                auto str = visitor.GetString(g_implG->iGetString(user));
                auto targetClass = visitor.GetString(userData.targetClass);
                isFind = targetClass == str;
            }
        });
    });
    return isFind;
}

struct VisitData {
    AbckitGraph *ctxG = nullptr;
    UserData *ud = nullptr;
    AbckitCoreImportDescriptor *ci = nullptr;
    AbckitInst *newInst = nullptr;
};

bool VisitBlock(AbckitBasicBlock *bb, void *data)
{
    auto *vData = reinterpret_cast<VisitData *>(data);
    auto *inst = g_implG->bbGetFirstInst(bb);
    while (inst != nullptr) {
        auto opcode = g_dynG->iGetOpcode(inst);
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1 ||
            opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLTHIS1WITHNAME) {
            auto *ldExternal = g_dynG->iCreateLdexternalmodulevar(vData->ctxG, vData->ci);
            if (ldExternal == nullptr) {
                return false;
            }
            auto *classThrow = g_dynG->iCreateThrowUndefinedifholewithname(
                vData->ctxG, ldExternal, g_implI->classGetName(vData->ud->classToReplace));
            if (classThrow == nullptr) {
                return false;
            }
            auto *constInst = g_implG->gFindOrCreateConstantI32(vData->ctxG, 5);
            if (constInst == nullptr) {
                return false;
            }
            auto *ldobj = g_dynG->iCreateLdobjbyname(vData->ctxG, ldExternal,
                                                     g_implI->functionGetName(vData->ud->methodToReplace));
            if (ldobj == nullptr) {
                return false;
            }
            auto *staticCall = g_dynG->iCreateCallthis2(vData->ctxG, ldobj, ldExternal, vData->newInst, constInst);
            if (staticCall == nullptr) {
                return false;
            }
            g_implG->iInsertAfter(ldExternal, inst);
            g_implG->iInsertAfter(classThrow, ldExternal);
            g_implG->iInsertAfter(ldobj, classThrow);
            g_implG->iInsertAfter(staticCall, ldobj);
        }
        inst = g_implG->iGetNext(inst);
    }
    return true;
}

static void CollectAnnoInfo(
    VisitHelper &visitor,
    std::vector<std::tuple<AbckitCoreClass *, AbckitCoreFunction *, AbckitCoreAnnotation *>> &infos,
    AbckitCoreClass *klass, AbckitCoreFunction *method)
{
    visitor.EnumerateMethodAnnotations(method, [&](AbckitCoreAnnotation *anno) {
        auto *annoClass = g_implI->annotationGetInterface(anno);
        auto annoName = visitor.GetString(g_implI->annotationInterfaceGetName(annoClass));
        if (annoName != ANNOTATION_INTERFACE_NAME) {
            return;
        }
        infos.emplace_back(klass, method, anno);
    });
}

static void FillAnnotationInfo(VisitHelper &visitor, AbckitCoreModule *mod, UserData &ud)
{
    std::vector<std::tuple<AbckitCoreClass *, AbckitCoreFunction *, AbckitCoreAnnotation *>> infos;
    visitor.EnumerateModuleClasses(mod, [&](AbckitCoreClass *klass) {
        visitor.EnumerateClassMethods(
            klass, [&](AbckitCoreFunction *method) { CollectAnnoInfo(visitor, infos, klass, method); });
    });
    for (auto &[klass, method, anno] : infos) {
        visitor.EnumerateAnnotationElements(anno, [&](AbckitCoreAnnotationElement *annoElem) {
            auto annoElemName = visitor.GetString(g_implI->annotationElementGetName(annoElem));
            auto *value = g_implI->annotationElementGetValue(annoElem);
            if (std::string_view(annoElemName) == "targetClass") {
                ud.targetClass = g_implI->valueGetString(value);
            }
            if (std::string_view(annoElemName) == "methodName") {
                ud.methodName = g_implI->valueGetString(value);
            }
        });
        ud.classToReplace = klass;
        ud.methodToReplace = method;
    }
}

// Add the ApiControl import ONCE on the main module (main thread). The module-import addition is a
// module-level mutation outside the D1/D4 Graph/function thread-safety guarantee, so it stays
// single-threaded; workers share the read-only coreImport descriptor.
static AbckitCoreImportDescriptor *AddImportOnce(AbckitFile *file, UserData &ud)
{
    (void)ud;  // ud not needed for the import (the params are constants)
    AbckitArktsImportFromDynamicModuleCreateParams params {};
    params.name = API_CLASS_NAME;
    params.alias = API_CLASS_NAME;

    helpers::ModuleByNameContext apiFinder {nullptr, API_MODULE_NAME};
    g_implI->fileEnumerateModules(file, &apiFinder, helpers::ModuleByNameFinder);
    if (apiFinder.module == nullptr) {
        return nullptr;
    }
    helpers::ModuleByNameContext mainFinder {nullptr, MAIN_MODULE_NAME};
    g_implI->fileEnumerateModules(file, &mainFinder, helpers::ModuleByNameFinder);
    if (mainFinder.module == nullptr) {
        return nullptr;
    }
    auto *newImport =
        g_implArkM->moduleAddImportFromArktsV1ToArktsV1(g_implArkI->coreModuleToArktsModule(mainFinder.module),
                                                        g_implArkI->coreModuleToArktsModule(apiFinder.module), &params);
    if (newImport == nullptr) {
        return nullptr;
    }
    return g_implArkI->arktsImportDescriptorToCoreImportDescriptor(newImport);
}

// Worker task: per-function call-site replacement using the shared import descriptor. Runs the D1
// (createGraphFromFunction) + D4 (functionSetGraph) paths on a distinct function; the import
// descriptor is read-only (used only to create ldExternalModulevar insts in this Graph).
static bool ReplaceCallSiteInFunction(AbckitCoreFunction *method, UserData &ud, AbckitCoreImportDescriptor *coreImport)
{
    auto ctxG = g_implI->createGraphFromFunction(method);
    if (ctxG == nullptr) {
        return false;
    }
    VisitData vd;
    vd.ctxG = ctxG;
    vd.ci = coreImport;
    vd.ud = &ud;
    vd.newInst = helpers::FindFirstInst(ctxG, ABCKIT_ISA_API_DYNAMIC_OPCODE_NEWOBJRANGE);
    if (vd.newInst == nullptr) {
        g_impl->destroyGraph(ctxG);
        return false;
    }
    g_implG->gVisitBlocksRpo(ctxG, &vd, [](AbckitBasicBlock *bb, void *data) -> bool { return VisitBlock(bb, data); });
    g_implM->functionSetGraph(method, ctxG);
    g_impl->destroyGraph(ctxG);
    return true;
}

// Parse the @CallSiteReplacement annotation + add the import on `file`. ud's pointers are into
// `file` — valid only while `file` is open.
static AbckitCoreImportDescriptor *SetupFile(AbckitFile *file, UserData &ud)
{
    auto visitor = VisitHelper(file, g_impl, g_implI, g_implG, g_dynG);
    visitor.EnumerateModules([&](AbckitCoreModule *mod) {
        if (visitor.GetString(g_implI->moduleGetName(mod)) != API_MODULE_NAME) {
            return;
        }
        FillAnnotationInfo(visitor, mod, ud);
    });
    if (ud.classToReplace == nullptr || ud.methodToReplace == nullptr) {
        return nullptr;
    }
    return AddImportOnce(file, ud);
}

static void EnumerateTargetMethods(AbckitFile *file, UserData &ud, const std::function<void(AbckitCoreFunction *)> &cb)
{
    auto visitor = VisitHelper(file, g_impl, g_implI, g_implG, g_dynG);
    visitor.EnumerateModules([&](AbckitCoreModule *mod) {
        if (visitor.GetString(g_implI->moduleGetName(mod)) != MAIN_MODULE_NAME) {
            return;
        }
        visitor.EnumerateModuleClasses(mod, [&](AbckitCoreClass *klass) {
            visitor.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
                if (HasSearchedMethod(visitor, method, ud)) {
                    cb(method);
                }
            });
        });
    });
}
}  // namespace

class LibAbcKitDynamicScenarioParallelTest : public ::testing::Test {};

// CC-OFFNXT(huge_method, C_RULE_ID_FUNCTION_SIZE) test, solid logic
// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicScenarioParallelTest, LibAbcKitTestDynamicReplaceCallSiteSerialParallel)
{
    // Origin: each fooN prints 3 (setOrientation(3) -> getOrientation()=3).
    auto origin = helpers::ExecuteDynamicAbc(INPUT_PATH, ENTRY);
    EXPECT_EQ(origin, "3\n3\n3\n3\n");

    const std::string expected = [] {
        std::string out;
        for (int i = 0; i < 4; ++i) {
            out += "fixOrientationLinearLayout was called\n5\n";
        }
        return out;
    }();

    // ---- SERIAL baseline: parse ud + add import + replace all four functions on one file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        UserData ud {};
        AbckitCoreImportDescriptor *coreImport = SetupFile(file, ud);
        ASSERT_NE(coreImport, nullptr);
        EnumerateTargetMethods(file, ud, [&](AbckitCoreFunction *method) {
            ASSERT_TRUE(ReplaceCallSiteInFunction(method, ud, coreImport));
        });
        g_impl->writeAbc(file, SERIAL_OUT_PATH, std::strlen(SERIAL_OUT_PATH));
        g_impl->closeFile(file);
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT_PATH, ENTRY);
    EXPECT_EQ(serialOut, expected);

    // ---- PARALLEL: parse ud + add import (main thread), 4 workers each replace a fooN ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        UserData ud {};
        AbckitCoreImportDescriptor *coreImport = SetupFile(file, ud);
        ASSERT_NE(coreImport, nullptr);
        std::vector<AbckitCoreFunction *> targets;
        EnumerateTargetMethods(file, ud, [&](AbckitCoreFunction *method) { targets.push_back(method); });
        ASSERT_EQ(targets.size(), 4U);

        std::atomic<bool> failed {false};
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (size_t i = 0; i < targets.size(); ++i) {
            threads.emplace_back([&ud, &coreImport, &targets, i, &failed]() {
                // D1 (createGraphFromFunction) + D4 (functionSetGraph) on a distinct function; the
                // shared coreImport is read-only.
                if (!ReplaceCallSiteInFunction(targets[i], ud, coreImport)) {
                    failed.store(true);
                }
            });
        }
        for (auto &t : threads) {
            t.join();
        }
        ASSERT_FALSE(failed.load());
        g_impl->writeAbc(file, PARALLEL_OUT_PATH, std::strlen(PARALLEL_OUT_PATH));
        g_impl->closeFile(file);
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT_PATH, ENTRY);
    EXPECT_EQ(parallelOut, expected);

    // Serial == parallel (deterministic output).
    EXPECT_EQ(serialOut, parallelOut);
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
